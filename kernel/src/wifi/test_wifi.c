/* test_wifi.c — the 802.11 subsystem against the standard, not against itself.
 *
 * Build & run:
 *   cc -std=c11 -Wall -Werror -Wextra -O1 -fsanitize=address,undefined \
 *      -DTEST_HOST -Iinclude -Isrc/wifi -Isrc/edp_risk -Isrc/surplus \
 *      src/wifi/test_wifi.c src/wifi/wifi.c -o /tmp/t_wifi && /tmp/t_wifi
 *
 * Three rules this file follows:
 *
 *  1. Anchors come from outside. The frames fed to the parsers are written
 *     here from the IEEE 802.11 layout (byte offsets, little-endian fields),
 *     not produced by our own builders, so a shared misunderstanding cannot
 *     pass. The crypto is checked against FIPS 180-1, RFC 2202, RFC 6070 and
 *     IEEE 802.11i Annex H.4 vectors.
 *
 *  2. Every check asserts a COMPUTED VALUE — a byte at an offset, a channel
 *     number, a digest, a packet count — never merely "the call returned 0".
 *
 *  3. Every verify/check function is also driven to FAIL: the EAPOL MIC is
 *     tested with a flipped byte, the coverage check with a corrupted
 *     interface, the state machine with every illegal edge.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h> /* malloc/free — exact-size buffers for the fuzz pass */
#include "wifi.h"

static int failures = 0;
static int checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", (m));                                                            \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", (m));                                                            \
    } while (0)

/* ---- helpers ---- */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Compare `n` bytes of `got` against a lowercase hex string. */
static bool hexeq(const uint8_t *got, const char *hex, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        int hi = hexval(hex[i * 2]), lo = hexval(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        if (got[i] != (uint8_t) ((hi << 4) | lo)) return false;
    }
    return hex[n * 2] == '\0';
}

static void dump(const char *tag, const uint8_t *b, uint32_t n)
{
    printf("       %s = ", tag);
    for (uint32_t i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

/* deterministic PRNG so ring/fuzz loops are reproducible */
static uint32_t rng_state = 0x1234567u;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* ===================================================================== */
/* A virtual radio: the ops struct a real driver would fill in.           */
/* It records every byte we hand it, so the tests can assert on the wire  */
/* image rather than on our intent.                                       */
/* ===================================================================== */

#define VR_TX_SLOTS 16
typedef struct {
    uint8_t tx[VR_TX_SLOTS][512];
    uint32_t tx_len[VR_TX_SLOTS];
    uint32_t tx_count;
    int tx_fail;

    uint8_t rx[8][512];
    uint32_t rx_len[8];
    uint32_t rx_head, rx_tail;
    int rx_fail;

    uint32_t scan_calls, ch_calls, ps_calls, ap_start_calls, ap_stop_calls;
    uint32_t txp_calls;
    int txp_fail;
    uint8_t last_dbm;
    uint32_t last_channel, last_freq;
    uint8_t last_ps;
    int have_random;
    uint8_t random_seed;
    int have_mac;
    uint8_t mac_base;
} vradio_t;

static int vr_tx(void *ctx, const uint8_t *f, uint32_t n)
{
    vradio_t *v = (vradio_t *) ctx;
    if (v->tx_fail) return -1;
    if (v->tx_count >= VR_TX_SLOTS || n > sizeof v->tx[0]) return -1;
    memcpy(v->tx[v->tx_count], f, n);
    v->tx_len[v->tx_count] = n;
    v->tx_count++;
    return (int) n;
}
static int vr_rx(void *ctx, uint8_t *f, uint32_t cap)
{
    vradio_t *v = (vradio_t *) ctx;
    if (v->rx_fail) return -1;
    if (v->rx_tail == v->rx_head) return 0;
    uint32_t n = v->rx_len[v->rx_tail % 8];
    if (n > cap) return -1;
    memcpy(f, v->rx[v->rx_tail % 8], n);
    v->rx_tail++;
    return (int) n;
}
static int vr_set_channel(void *ctx, uint32_t ch, uint32_t freq)
{
    vradio_t *v = (vradio_t *) ctx;
    v->ch_calls++;
    v->last_channel = ch;
    v->last_freq = freq;
    return 0;
}
static int vr_scan_start(void *ctx, uint32_t hint)
{
    vradio_t *v = (vradio_t *) ctx;
    (void) hint;
    v->scan_calls++;
    return 0;
}
static int vr_set_ps(void *ctx, uint8_t lvl)
{
    vradio_t *v = (vradio_t *) ctx;
    v->ps_calls++;
    v->last_ps = lvl;
    return 0;
}
static int vr_set_txp(void *ctx, uint8_t dbm)
{
    vradio_t *v = (vradio_t *) ctx;
    if (v->txp_fail) return -1;
    v->txp_calls++;
    v->last_dbm = dbm;
    return 0;
}
static int vr_ap_start(void *ctx, const char *ssid, uint32_t ch)
{
    vradio_t *v = (vradio_t *) ctx;
    (void) ssid;
    v->ap_start_calls++;
    v->last_channel = ch;
    return 0;
}
static int vr_ap_stop(void *ctx)
{
    ((vradio_t *) ctx)->ap_stop_calls++;
    return 0;
}
static int vr_random(void *ctx, uint8_t *out, uint32_t n)
{
    vradio_t *v = (vradio_t *) ctx;
    if (!v->have_random) return -1;
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t) (v->random_seed + i);
    return (int) n;
}
static int vr_get_mac(void *ctx, uint32_t idx, uint8_t mac[6])
{
    vradio_t *v = (vradio_t *) ctx;
    if (!v->have_mac) return -1;
    mac[0] = 0x00;
    mac[1] = 0x1A;
    mac[2] = 0x2B;
    mac[3] = 0x3C;
    mac[4] = v->mac_base;
    mac[5] = (uint8_t) idx;
    return 0;
}

static const wifi_ops_t VRADIO_OPS = {
    vr_scan_start, vr_tx,      vr_rx,     vr_set_channel, vr_set_ps, vr_set_txp,
    vr_ap_start,   vr_ap_stop, vr_random, vr_get_mac,     0};

/* Returns an ops struct pointing at `v`; the caller keeps it alive and hands
 * &ops to wifi_bind_ops(). */
static wifi_ops_t vradio_ops_for(vradio_t *v)
{
    wifi_ops_t ops = VRADIO_OPS;
    ops.ctx = v;
    return ops;
}

/* ===================================================================== */
/* Frame builders written from the 802.11 layout, NOT from wifi.c        */
/* ===================================================================== */

static void put16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

/* A management header, by hand: fc(2) dur(2) a1(6) a2(6) a3(6) seq(2) = 24 */
static uint32_t mgmt_hdr(uint8_t *o, uint8_t subtype, const uint8_t a1[6], const uint8_t a2[6],
                         const uint8_t a3[6], uint16_t seq)
{
    memset(o, 0, 24);
    o[0] = (uint8_t) (subtype << 4); /* type = 0 (mgmt), version = 0 */
    o[1] = 0;
    put16le(o + 2, 0);
    memcpy(o + 4, a1, 6);
    memcpy(o + 10, a2, 6);
    memcpy(o + 16, a3, 6);
    put16le(o + 22, (uint16_t) (seq << 4));
    return 24;
}

enum { SEC_OPEN = 0, SEC_WEP, SEC_WPA1, SEC_WPA2, SEC_WPA3, SEC_BOTH };
enum { PHY_B = 0, PHY_G, PHY_N, PHY_AC, PHY_AX };

static uint32_t make_beacon(uint8_t *o, const uint8_t bssid[6], const char *ssid, uint8_t channel,
                            int sec, int phy)
{
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t p = mgmt_hdr(o, WIFI_STYPE_BEACON, bcast, bssid, bssid, 7);
    memset(o + p, 0, 8);
    p += 8; /* TSF timestamp */
    put16le(o + p, 100);
    p += 2; /* beacon interval */
    put16le(o + p, (uint16_t) (0x0001 | (sec != SEC_OPEN ? 0x0010 : 0)));
    p += 2;

    uint32_t slen = (uint32_t) strlen(ssid);
    o[p++] = WIFI_EID_SSID;
    o[p++] = (uint8_t) slen;
    memcpy(o + p, ssid, slen);
    p += slen;

    o[p++] = WIFI_EID_RATES;
    o[p++] = 4;
    o[p++] = 0x82;
    o[p++] = 0x84;
    o[p++] = 0x8B;
    o[p++] = 0x96; /* 1,2,5.5,11 */

    o[p++] = WIFI_EID_DSPARAM;
    o[p++] = 1;
    o[p++] = channel;

    if (phy != PHY_B) { /* OFDM rates: top one is 0x6C = 108 * 500kbps = 54 Mbps */
        o[p++] = WIFI_EID_EXT_RATES;
        o[p++] = 8;
        o[p++] = 0x0C;
        o[p++] = 0x12;
        o[p++] = 0x18;
        o[p++] = 0x24;
        o[p++] = 0x30;
        o[p++] = 0x48;
        o[p++] = 0x60;
        o[p++] = 0x6C;
    }
    if (phy >= PHY_N) {
        o[p++] = WIFI_EID_HT_CAP;
        o[p++] = 2;
        o[p++] = 0;
        o[p++] = 0;
    }
    if (phy >= PHY_AC) {
        o[p++] = WIFI_EID_VHT_CAP;
        o[p++] = 2;
        o[p++] = 0;
        o[p++] = 0;
    }
    if (phy >= PHY_AX) {
        o[p++] = WIFI_EID_EXTENSION;
        o[p++] = 3;
        o[p++] = WIFI_EID_EXT_HE_CAP;
        o[p++] = 0;
        o[p++] = 0;
    }

    if (sec == SEC_WPA1) {
        o[p++] = WIFI_EID_VENDOR;
        o[p++] = 6;
        o[p++] = 0x00;
        o[p++] = 0x50;
        o[p++] = 0xF2;
        o[p++] = 0x01;
        o[p++] = 0x01;
        o[p++] = 0x00;
    } else if (sec == SEC_WPA2 || sec == SEC_WPA3 || sec == SEC_BOTH) {
        uint32_t akm_n = (sec == SEC_BOTH) ? 2 : 1;
        o[p++] = WIFI_EID_RSN;
        o[p++] = (uint8_t) (2 + 4 + 2 + 4 + 2 + akm_n * 4 + 2);
        put16le(o + p, 1);
        p += 2; /* version */
        o[p++] = 0x00;
        o[p++] = 0x0F;
        o[p++] = 0xAC;
        o[p++] = 0x04; /* group CCMP */
        put16le(o + p, 1);
        p += 2;
        o[p++] = 0x00;
        o[p++] = 0x0F;
        o[p++] = 0xAC;
        o[p++] = 0x04; /* pairwise CCMP */
        put16le(o + p, (uint16_t) akm_n);
        p += 2;
        if (sec == SEC_WPA2 || sec == SEC_BOTH) {
            o[p++] = 0x00;
            o[p++] = 0x0F;
            o[p++] = 0xAC;
            o[p++] = 0x02; /* AKM PSK */
        }
        if (sec == SEC_WPA3 || sec == SEC_BOTH) {
            o[p++] = 0x00;
            o[p++] = 0x0F;
            o[p++] = 0xAC;
            o[p++] = 0x08; /* AKM SAE */
        }
        put16le(o + p, 0);
        p += 2; /* RSN caps */
    }
    return p;
}

/* An EAPOL-Key frame from the 802.1X layout. If kck is non-NULL the MIC is
 * computed the naive way — one contiguous buffer, MIC zeroed, one HMAC call —
 * which is an independent path from the three-piece streaming HMAC in
 * wifi_eapol_mic(). */
static uint32_t make_eapol(uint8_t *o, uint16_t key_info, uint64_t replay, const uint8_t nonce[32],
                           const uint8_t *kd, uint16_t kdlen, const uint8_t *kck)
{
    uint32_t total = 99u + kdlen;
    memset(o, 0, total);
    o[0] = 2; /* 802.1X-2004 */
    o[1] = 3; /* EAPOL-Key   */
    o[2] = (uint8_t) ((total - 4) >> 8);
    o[3] = (uint8_t) (total - 4);
    o[4] = 2; /* RSN key descriptor */
    o[5] = (uint8_t) (key_info >> 8);
    o[6] = (uint8_t) key_info;
    o[7] = 0;
    o[8] = 16; /* key length */
    for (int i = 0; i < 8; i++) o[9 + i] = (uint8_t) (replay >> (8 * (7 - i)));
    if (nonce) memcpy(o + 17, nonce, 32);
    o[97] = (uint8_t) (kdlen >> 8);
    o[98] = (uint8_t) kdlen;
    if (kdlen && kd) memcpy(o + 99, kd, kdlen);
    if (kck) {
        uint8_t digest[20];
        wifi_hmac_sha1(kck, 16, o, total, digest); /* MIC field is already 0 */
        memcpy(o + 81, digest, 16);
    }
    return total;
}

/* ===================================================================== */

int main(void)
{
    static wifi_device_t dev;
    static vradio_t vr;
    uint8_t buf[1024];

    printf("=== ZXV 802.11 subsystem ===\n\n");

    /* ================================================================= */
    printf("--- init and the no-radio boundary ---\n");
    /* ================================================================= */
    wifi_init(0, "null-dev"); /* the ARM32 boot path really does this */
    CHECK(true, "wifi_init(NULL) returns instead of dereferencing (ARM32 boot path)");

    wifi_init(&dev, "zxv-wifi0");
    CHECK(dev.num_ifaces == 0 && dev.num_scan_results == 0,
          "a fresh device has 0 interfaces and 0 scan results");
    CHECK(dev.ops == 0 && !wifi_has_radio(&dev), "no radio is bound after init");
    CHECK(dev.supports_wpa3 == false,
          "supports_wpa3 is FALSE — there is no SAE here, so claiming it would be a lie");

    uint32_t sta = wifi_create_interface(&dev, "wlan0", WIFI_MODE_STATION);
    CHECK(sta == 1, "the first interface gets id 1 (0 is reserved for failure)");
    wifi_interface_t *f = wifi_get_interface(&dev, sta);
    CHECK(f != 0 && f->state == WIFI_STATE_IDLE, "it starts IDLE");
    CHECK(f->mac_is_placeholder && (f->mac[0] & 0x02) && !(f->mac[0] & 0x01),
          "its MAC is a flagged locally-administered placeholder, not a fake OUI");
    CHECK(wifi_get_interface(&dev, 0) == 0 && wifi_get_interface(&dev, 99) == 0,
          "out-of-range interface ids resolve to NULL, not to memory");

    /* The whole point: with no radio nothing succeeds and nothing is invented. */
    CHECK(wifi_scan(&dev, sta) == WIFI_ENODEV, "wifi_scan with no radio -> WIFI_ENODEV");
    CHECK(wifi_get_scan_count(&dev) == 0,
          "...and it invented ZERO access points (num_scan_results still 0)");
    CHECK(wifi_get_scan_result(&dev, 0) == 0, "...and result 0 is NULL, not a ghost AP");
    CHECK(f->state == WIFI_STATE_IDLE, "...and the interface did not enter SCANNING");
    CHECK(wifi_connect(&dev, sta, "any", "password1", WIFI_SEC_WPA2) == WIFI_ENODEV,
          "wifi_connect with no radio -> WIFI_ENODEV");
    CHECK(!f->connected && f->state == WIFI_STATE_IDLE,
          "...and the interface did NOT become connected");
    CHECK(wifi_set_channel(&dev, sta, 6) == WIFI_ENODEV, "set_channel with no radio -> ENODEV");
    CHECK(f->channel == 0, "...and the cached channel did not move");
    CHECK(wifi_set_power_save(&dev, sta, 1) == WIFI_ENODEV, "set_power_save -> ENODEV");
    CHECK(wifi_tx_packet(&dev, sta, "hello", 5) == WIFI_ENODEV, "tx_packet -> ENODEV");
    CHECK(f->tx_packets == 0 && f->tx_bytes == 0,
          "...and NO counter moved for a packet that was never sent");
    CHECK(wifi_rx_packet(&dev, sta, buf, sizeof buf) == WIFI_ENODEV, "rx_packet -> ENODEV");
    CHECK(wifi_start_ap(&dev, sta, "ap", 0, 6) == WIFI_ESTATE,
          "start_ap on a STATION interface -> WIFI_ESTATE (mode is checked first)");

    /* Argument validation happens before the backend is consulted. */
    CHECK(wifi_set_channel(&dev, sta, 15) == WIFI_EINVAL,
          "channel 15 is EINVAL even with no radio (args validated first)");
    CHECK(wifi_set_power_save(&dev, sta, 9) == WIFI_EINVAL, "power-save level 9 is EINVAL");
    CHECK(wifi_connect(&dev, sta, "x", "short", WIFI_SEC_WPA2) == WIFI_EINVAL,
          "a 5-character WPA2 passphrase is EINVAL (802.11i requires 8..63)");
    CHECK(wifi_connect(&dev, sta, "x", "password1", WIFI_SEC_WPA3) == WIFI_ENOTSUP,
          "WPA3 is ENOTSUP — SAE is not implemented and is not pretended (L6)");
    CHECK(wifi_connect(&dev, sta, "x", "hello", WIFI_SEC_WEP) == WIFI_ENOTSUP,
          "WEP is ENOTSUP — no RC4 here");
    CHECK(wifi_bind_ops(&dev, 0) == WIFI_EINVAL, "binding a NULL ops struct is refused");
    {
        wifi_ops_t empty;
        memset(&empty, 0, sizeof empty);
        CHECK(wifi_bind_ops(&dev, &empty) == WIFI_EINVAL,
              "an ops struct that can neither tx nor rx is not a radio");
    }

    /* ================================================================= */
    printf("\n--- channel <-> frequency (2.4 / 5 / 6 GHz) ---\n");
    /* ================================================================= */
    CHECK(wifi_channel_to_freq(1, WIFI_BAND_2_4GHZ) == 2412, "2.4 GHz ch 1  = 2412 MHz");
    CHECK(wifi_channel_to_freq(6, WIFI_BAND_2_4GHZ) == 2437, "2.4 GHz ch 6  = 2437 MHz");
    CHECK(wifi_channel_to_freq(11, WIFI_BAND_2_4GHZ) == 2462, "2.4 GHz ch 11 = 2462 MHz");
    CHECK(wifi_channel_to_freq(13, WIFI_BAND_2_4GHZ) == 2472, "2.4 GHz ch 13 = 2472 MHz");
    CHECK(wifi_channel_to_freq(14, WIFI_BAND_2_4GHZ) == 2484,
          "2.4 GHz ch 14 = 2484 MHz (the 12 MHz gap, not 2477)");
    CHECK(wifi_channel_to_freq(15, WIFI_BAND_2_4GHZ) == 0, "2.4 GHz ch 15 does not exist");
    CHECK(wifi_channel_to_freq(0, WIFI_BAND_2_4GHZ) == 0, "channel 0 does not exist");
    CHECK(wifi_channel_to_freq(36, WIFI_BAND_5GHZ) == 5180, "5 GHz ch 36  = 5180 MHz");
    CHECK(wifi_channel_to_freq(100, WIFI_BAND_5GHZ) == 5500, "5 GHz ch 100 = 5500 MHz");
    CHECK(wifi_channel_to_freq(165, WIFI_BAND_5GHZ) == 5825, "5 GHz ch 165 = 5825 MHz");
    CHECK(wifi_channel_to_freq(177, WIFI_BAND_5GHZ) == 5885, "5 GHz ch 177 = 5885 MHz");
    CHECK(wifi_channel_to_freq(37, WIFI_BAND_5GHZ) == 0,
          "5 GHz ch 37 is refused — arithmetic would happily return 5185 MHz");
    CHECK(wifi_channel_to_freq(1, WIFI_BAND_6GHZ) == 5955, "6 GHz ch 1   = 5955 MHz");
    CHECK(wifi_channel_to_freq(2, WIFI_BAND_6GHZ) == 5935,
          "6 GHz ch 2 = 5935 MHz (the one channel that breaks the pattern)");
    CHECK(wifi_channel_to_freq(233, WIFI_BAND_6GHZ) == 7115, "6 GHz ch 233 = 7115 MHz");
    CHECK(wifi_channel_to_freq(3, WIFI_BAND_6GHZ) == 0, "6 GHz ch 3 is not a 20 MHz channel");
    CHECK(wifi_freq_to_channel(2412) == 1 && wifi_freq_to_channel(2484) == 14,
          "2412 -> ch 1 and 2484 -> ch 14");
    CHECK(wifi_freq_to_channel(5180) == 36 && wifi_freq_to_channel(5955) == 1,
          "5180 -> ch 36 and 5955 -> ch 1 (6 GHz)");
    CHECK(wifi_freq_to_channel(5935) == 2, "5935 -> 6 GHz ch 2");
    CHECK(wifi_freq_to_channel(2415) == 0 && wifi_freq_to_channel(1000) == 0,
          "a frequency between channels maps to nothing");
    CHECK(wifi_freq_to_band(2437) == (int) WIFI_BAND_2_4GHZ &&
              wifi_freq_to_band(5500) == (int) WIFI_BAND_5GHZ &&
              wifi_freq_to_band(6175) == (int) WIFI_BAND_6GHZ,
          "bands resolve from frequency");
    CHECK(wifi_freq_to_band(3000) == WIFI_EINVAL, "3000 MHz is in no Wi-Fi band");
    {
        /* Round-trip every channel we claim exists, in all three bands. */
        int rt_ok = 1, counted = 0;
        for (uint32_t ch = 1; ch <= 250; ch++) {
            for (int b = 0; b < 3; b++) {
                if (!wifi_channel_valid(ch, (wifi_band_t) b)) continue;
                uint32_t fq = wifi_channel_to_freq(ch, (wifi_band_t) b);
                if (fq == 0) {
                    rt_ok = 0;
                    break;
                }
                if (wifi_freq_to_channel(fq) != ch) {
                    rt_ok = 0;
                    break;
                }
                if (wifi_freq_to_band(fq) != b) {
                    rt_ok = 0;
                    break;
                }
                counted++;
            }
        }
        printf("       round-tripped %d (channel,band) pairs\n", counted);
        CHECK(rt_ok && counted == 105,
              "all 105 valid (channel,band) pairs round-trip channel->freq->channel/band");
    }
    /* The channel-side sweep above only ever feeds VALID channels, so it
     * cannot catch a frequency that is NOT a channel centre but which the
     * arithmetic is willing to name one. Drive the mapping from the frequency
     * side too, over every megahertz in every band. */
    CHECK(wifi_freq_to_channel(5185) == 0,
          "5185 MHz is not an operating centre: arithmetic would call it 5 GHz channel 37");
    CHECK(wifi_freq_to_channel(5360) == 0 && wifi_freq_to_channel(5460) == 0,
          "5360 and 5460 MHz fall in the 68..96 gap: 'channels' 72 and 92 do not exist");
    CHECK(wifi_freq_to_channel(5340) == 68 && wifi_freq_to_channel(5480) == 96,
          "...while 5340 and 5480 MHz, which bracket that gap, ARE channels 68 and 96");
    CHECK(wifi_freq_to_channel(5960) == 0,
          "5960 MHz is NOT 6 GHz channel 2 — channel 2 sits alone at 5935, and naming "
          "5960 'channel 2' would hand a driver a channel whose centre is a different MHz");
    CHECK(wifi_freq_to_channel(5965) == 0 && wifi_freq_to_channel(5970) == 0,
          "5965 and 5970 MHz are not 6 GHz channel centres either");
    {
        /* THE invariant: if freq_to_channel names a channel, that channel's
         * own centre frequency must be the frequency we started from. Anything
         * else is a mapping that contradicts itself. */
        int closed = 1, named = 0;
        uint32_t worst_f = 0, worst_ch = 0, worst_back = 0;
        for (uint32_t fq = 2000; fq <= 7200; fq++) {
            uint32_t ch = wifi_freq_to_channel(fq);
            if (ch == 0) continue;
            named++;
            int band = wifi_freq_to_band(fq);
            if (band < 0) {
                closed = 0;
                worst_f = fq;
                worst_ch = ch;
                worst_back = 0;
                break;
            }
            uint32_t back = wifi_channel_to_freq(ch, (wifi_band_t) band);
            if (back != fq) {
                closed = 0;
                worst_f = fq;
                worst_ch = ch;
                worst_back = back;
                break;
            }
        }
        if (!closed)
            printf("       %u MHz -> ch %u -> %u MHz (does not close)\n", worst_f, worst_ch,
                   worst_back);
        printf("       %d of 5201 megahertz steps name a channel\n", named);
        CHECK(closed && named == 105,
              "every frequency that names a channel maps BACK to itself, and exactly 105 "
              "megahertz values in 2000..7200 are channel centres");
    }

    /* ================================================================= */
    printf("\n--- 802.11 MAC header: build and parse against the layout ---\n");
    /* ================================================================= */
    {
        uint8_t a1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
        uint8_t a2[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        uint8_t a3[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        wifi_mac_hdr_t h;
        memset(&h, 0, sizeof h);
        h.type = WIFI_FTYPE_DATA;
        h.subtype = WIFI_STYPE_DATA;
        h.to_ds = true;
        h.retry = true;
        h.duration_id = 0x1234;
        memcpy(h.addr1, a1, 6);
        memcpy(h.addr2, a2, 6);
        memcpy(h.addr3, a3, 6);
        h.seq_num = 2050;
        h.frag_num = 5;

        int n = wifi_mac_hdr_build(&h, buf, sizeof buf);
        CHECK(n == 24, "a 3-address non-QoS header is exactly 24 bytes");
        CHECK(buf[0] == 0x08,
              "frame control byte 0 = 0x08: version 0, type 2 (data) in bits 2-3, subtype 0");
        CHECK(buf[1] == 0x09, "byte 1 = 0x09: ToDS (0x01) | Retry (0x08)");
        CHECK(buf[2] == 0x34 && buf[3] == 0x12, "duration 0x1234 is LITTLE endian at offset 2");
        CHECK(memcmp(buf + 4, a1, 6) == 0, "addr1 at offset 4");
        CHECK(memcmp(buf + 10, a2, 6) == 0, "addr2 at offset 10");
        CHECK(memcmp(buf + 16, a3, 6) == 0, "addr3 at offset 16");
        CHECK(buf[22] == 0x25 && buf[23] == 0x80,
              "seq control at 22 packs frag 5 in bits 0-3 and seq 2050 in bits 4-15");

        wifi_mac_hdr_t p;
        int pn = wifi_mac_hdr_parse(buf, (uint32_t) n, &p);
        CHECK(pn == 24, "parsing consumes the same 24 bytes");
        CHECK(p.type == WIFI_FTYPE_DATA && p.subtype == 0, "type/subtype survive");
        CHECK(p.to_ds && !p.from_ds && p.retry && !p.pwr_mgmt, "the DS and Retry bits survive");
        CHECK(p.duration_id == 0x1234, "duration survives");
        CHECK(p.seq_num == 2050 && p.frag_num == 5, "sequence 2050 / fragment 5 survive");

        /* QoS data: +2 bytes of QoS control */
        h.subtype = WIFI_STYPE_QOS_DATA;
        h.qos_ctl = 0x0007;
        n = wifi_mac_hdr_build(&h, buf, sizeof buf);
        CHECK(n == 26, "a QoS data header is 26 bytes");
        CHECK(buf[0] == 0x88, "QoS data frame control byte 0 = 0x88 (subtype 8, type 2)");
        CHECK(buf[24] == 0x07 && buf[25] == 0x00, "QoS control is LE at offset 24");
        memset(&p, 0, sizeof p);
        CHECK(wifi_mac_hdr_parse(buf, (uint32_t) n, &p) == 26 && p.qos && p.qos_ctl == 7,
              "the parser recognises QoS and recovers the control field");

        /* 4-address WDS: +6 */
        uint8_t a4[6] = {0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC};
        h.subtype = WIFI_STYPE_DATA;
        h.from_ds = true;
        memcpy(h.addr4, a4, 6);
        n = wifi_mac_hdr_build(&h, buf, sizeof buf);
        CHECK(n == 30, "a 4-address non-QoS header is 30 bytes");
        CHECK(memcmp(buf + 24, a4, 6) == 0, "addr4 sits at offset 24");
        h.subtype = WIFI_STYPE_QOS_DATA;
        CHECK(wifi_mac_hdr_build(&h, buf, sizeof buf) == 32,
              "4 addresses plus QoS is the 32-byte maximum");
        memset(&p, 0, sizeof p);
        CHECK(wifi_mac_hdr_parse(buf, 32, &p) == 32 && p.has_addr4 && memcmp(p.addr4, a4, 6) == 0,
              "the parser finds addr4 behind the QoS field");

        /* wifi_mac_hdr_len is public and must agree with what the builder
         * actually writes for all four shapes — tested directly, because the
         * builder computes its own offset and would keep working even if this
         * function drifted, leaving only the capacity check wrong. */
        {
            wifi_mac_hdr_t L;
            memset(&L, 0, sizeof L);
            L.type = WIFI_FTYPE_DATA;
            L.subtype = WIFI_STYPE_DATA;
            CHECK(wifi_mac_hdr_len(&L) == 24, "hdr_len: 3 addresses, no QoS = 24");
            L.subtype = WIFI_STYPE_QOS_DATA;
            CHECK(wifi_mac_hdr_len(&L) == 26, "hdr_len: 3 addresses + QoS = 26");
            L.subtype = WIFI_STYPE_DATA;
            L.to_ds = true;
            L.from_ds = true;
            CHECK(wifi_mac_hdr_len(&L) == 30, "hdr_len: 4 addresses, no QoS = 30");
            L.subtype = WIFI_STYPE_QOS_DATA;
            CHECK(wifi_mac_hdr_len(&L) == 32, "hdr_len: 4 addresses + QoS = 32");
            L.type = WIFI_FTYPE_MGMT;
            L.subtype = WIFI_STYPE_BEACON;
            L.to_ds = false;
            L.from_ds = false;
            CHECK(wifi_mac_hdr_len(&L) == 24,
                  "hdr_len: management subtype 8 is NOT QoS data, so still 24");
            CHECK(wifi_mac_hdr_len(0) == 0, "hdr_len of NULL is 0, not a dereference");
            /* and each length really is the builder's capacity boundary */
            uint8_t tight[40];
            wifi_mac_hdr_t B;
            memset(&B, 0, sizeof B);
            B.type = WIFI_FTYPE_DATA;
            B.subtype = WIFI_STYPE_QOS_DATA;
            B.to_ds = true;
            B.from_ds = true;
            CHECK(wifi_mac_hdr_build(&B, tight, 31) == WIFI_EMSGSIZE,
                  "a 32-byte header is refused by a 31-byte buffer");
            CHECK(wifi_mac_hdr_build(&B, tight, 32) == 32, "...and fits exactly in 32");
            B.subtype = WIFI_STYPE_DATA;
            CHECK(wifi_mac_hdr_build(&B, tight, 29) == WIFI_EMSGSIZE,
                  "a 30-byte 4-address header is refused by a 29-byte buffer");
            CHECK(wifi_mac_hdr_build(&B, tight, 30) == 30, "...and fits exactly in 30");
        }

        /* Refusals */
        CHECK(wifi_mac_hdr_build(&h, buf, 20) == WIFI_EMSGSIZE,
              "building into a 20-byte buffer is refused, not truncated");
        CHECK(wifi_mac_hdr_parse(buf, 23, &p) == WIFI_EINVAL,
              "a 23-byte frame is too short to be a header");
        CHECK(wifi_mac_hdr_parse(buf, 30, &p) == WIFI_EINVAL,
              "a 4-addr QoS frame truncated to 30 bytes is refused (QoS field missing)");
        h.order = true;
        CHECK(wifi_mac_hdr_build(&h, buf, sizeof buf) == WIFI_ENOTSUP,
              "the +HTC (Order) variant is refused rather than mis-sized by 4 bytes");
        h.order = false;
        {
            /* The PARSER must refuse it too, and for the same reason: with a
             * 4-byte HT Control field present, every offset after the header
             * is wrong, so returning a header length would corrupt whatever
             * the caller reads next. Build a legal frame, then set the Order
             * bit on the wire. */
            uint8_t htc[64];
            wifi_mac_hdr_t hh;
            memset(&hh, 0, sizeof hh);
            hh.type = WIFI_FTYPE_DATA;
            hh.subtype = WIFI_STYPE_QOS_DATA;
            hh.seq_num = 1;
            int hn = wifi_mac_hdr_build(&hh, htc, sizeof htc);
            CHECK(hn == 26, "a QoS data header without Order is 26 bytes");
            CHECK(wifi_mac_hdr_parse(htc, (uint32_t) hn, &p) == 26,
                  "...and parses back to 26 while the Order bit is clear");
            htc[1] |= 0x80; /* Order — the +HTC variant */
            CHECK(wifi_mac_hdr_parse(htc, (uint32_t) hn, &p) == WIFI_ENOTSUP,
                  "setting the Order bit on the wire makes the PARSER refuse it too (L8), "
                  "instead of reporting a length that is 4 bytes short");
            { /* and wifi_parse_beacon, which is built on the same parser */
                uint8_t ob[128];
                wifi_scan_result_t r_htc;
                memset(ob, 0, sizeof ob);
                ob[0] = 0x80; /* beacon */
                ob[1] = 0x80; /* Order */
                CHECK(wifi_parse_beacon(ob, 60, -40, 2437, &r_htc) == WIFI_ENOTSUP,
                      "a beacon with the Order bit set is refused by wifi_parse_beacon");
            }
        }
        h.seq_num = 4096;
        CHECK(wifi_mac_hdr_build(&h, buf, sizeof buf) == WIFI_EINVAL,
              "sequence 4096 is out of the 12-bit field");
        h.seq_num = 0;
        h.frag_num = 16;
        CHECK(wifi_mac_hdr_build(&h, buf, sizeof buf) == WIFI_EINVAL,
              "fragment 16 is out of the 4-bit field");
        {
            uint8_t bad[24];
            memset(bad, 0, sizeof bad);
            bad[0] = 0x01; /* protocol version 1 */
            CHECK(wifi_mac_hdr_parse(bad, 24, &p) == WIFI_ENOTSUP,
                  "protocol version != 0 is refused");
        }
    }

    /* address roles for the four DS combinations */
    {
        wifi_mac_hdr_t h;
        memset(&h, 0, sizeof h);
        uint8_t A[6] = {1, 1, 1, 1, 1, 1}, B[6] = {2, 2, 2, 2, 2, 2}, C[6] = {3, 3, 3, 3, 3, 3};
        uint8_t da[6], sa[6], bss[6];
        memcpy(h.addr1, A, 6);
        memcpy(h.addr2, B, 6);
        memcpy(h.addr3, C, 6);

        h.to_ds = false;
        h.from_ds = false;
        CHECK(wifi_frame_addrs(&h, da, sa, bss) == WIFI_OK && da[0] == 1 && sa[0] == 2 &&
                  bss[0] == 3,
              "IBSS/mgmt (0,0): addr1=DA addr2=SA addr3=BSSID");
        h.to_ds = false;
        h.from_ds = true;
        CHECK(wifi_frame_addrs(&h, da, sa, bss) == WIFI_OK && da[0] == 1 && bss[0] == 2 &&
                  sa[0] == 3,
              "from the AP (0,1): addr1=DA addr2=BSSID addr3=SA");
        h.to_ds = true;
        h.from_ds = false;
        CHECK(wifi_frame_addrs(&h, da, sa, bss) == WIFI_OK && bss[0] == 1 && sa[0] == 2 &&
                  da[0] == 3,
              "to the AP (1,0): addr1=BSSID addr2=SA addr3=DA");
        h.to_ds = true;
        h.from_ds = true;
        CHECK(wifi_frame_addrs(&h, da, sa, bss) == WIFI_ENOTSUP,
              "WDS (1,1) has no single BSSID, so it is refused rather than guessed");
    }

    /* sequence numbers wrap at 4096 */
    {
        wifi_interface_t tmp;
        memset(&tmp, 0, sizeof tmp);
        tmp.seq_num = 4094;
        CHECK(wifi_next_seq(&tmp) == 4094 && wifi_next_seq(&tmp) == 4095 &&
                  wifi_next_seq(&tmp) == 0 && tmp.seq_num == 1,
              "sequence numbers run 4094, 4095, 0 — they wrap at 4096, not at 65536");
    }

    /* ================================================================= */
    printf("\n--- SSID / BSSID handling ---\n");
    /* ================================================================= */
    {
        char ssid[33];
        CHECK(wifi_ssid_set(ssid, "ZXV-NET") == 7 && strcmp(ssid, "ZXV-NET") == 0,
              "a 7-character SSID is stored and reported as 7 bytes");
        char max32[40];
        memset(max32, 'q', 32);
        max32[32] = '\0';
        CHECK(wifi_ssid_set(ssid, max32) == 32, "a 32-octet SSID is accepted (the limit)");
        max32[32] = 'q';
        max32[33] = '\0';
        CHECK(wifi_ssid_set(ssid, max32) == WIFI_EINVAL,
              "a 33-octet SSID is refused, not truncated");
        CHECK(wifi_ssid_set(ssid, "") == 0, "an empty SSID (wildcard) is legal");
        CHECK(wifi_ssid_equal("abc", "abc") && !wifi_ssid_equal("abc", "abcd") &&
                  !wifi_ssid_equal("abc", "abC"),
              "SSID comparison is exact and case sensitive");

        uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
        uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
        CHECK(wifi_bssid_is_zero(zero) && !wifi_bssid_is_zero(mac), "zero BSSID detected");
        CHECK(wifi_bssid_is_broadcast(bc) && !wifi_bssid_is_broadcast(mac),
              "broadcast BSSID detected");
        char txt[20];
        CHECK(wifi_bssid_format(mac, txt, sizeof txt) == 17 &&
                  strcmp(txt, "de:ad:be:ef:01:02") == 0,
              "BSSID formats as de:ad:be:ef:01:02");
        CHECK(wifi_bssid_format(mac, txt, 17) == WIFI_EMSGSIZE,
              "a 17-byte buffer cannot hold 17 characters plus a terminator");
    }

    /* ================================================================= */
    printf("\n--- beacon / probe-response parsing ---\n");
    /* ================================================================= */
    {
        uint8_t bssid[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        uint8_t bcn[512];
        wifi_scan_result_t r;

        uint32_t bn = make_beacon(bcn, bssid, "ZXV-NET", 6, SEC_WPA2, PHY_G);
        printf("       hand-built beacon is %u bytes\n", bn);
        CHECK(wifi_parse_beacon(bcn, bn, -47, 2437, &r) == WIFI_OK, "a WPA2 beacon parses");
        CHECK(strcmp(r.ssid, "ZXV-NET") == 0, "SSID from element 0 is \"ZXV-NET\"");
        CHECK(memcmp(r.bssid, bssid, 6) == 0, "BSSID comes from addr3");
        CHECK(r.channel == 6, "channel 6, cross-checked against the 2437 MHz it was heard on");
        CHECK(r.band == WIFI_BAND_2_4GHZ, "band resolves to 2.4 GHz");
        CHECK(r.rssi == -47, "the RSSI we measured is carried through");
        CHECK(r.security == WIFI_SEC_WPA2, "AKM 00-0F-AC-02 (PSK) reads as WPA2");
        CHECK(r.max_rate_mbps == 54,
              "max legacy rate is 54 Mbps: 0x6C = 108 half-megabits (L7: MCS not decoded)");
        CHECK(r.standard == WIFI_802_11G, "OFDM rates and no HT element means 802.11g");
        CHECK(!r.hidden, "a named network is not hidden");

        bn = make_beacon(bcn, bssid, "ZXV-NET", 6, SEC_WPA3, PHY_AX);
        CHECK(wifi_parse_beacon(bcn, bn, -60, 2437, &r) == WIFI_OK && r.security == WIFI_SEC_WPA3,
              "AKM 00-0F-AC-08 (SAE) reads as WPA3");
        CHECK(r.standard == WIFI_802_11AX, "the HE capability element means 802.11ax");
        CHECK(r.max_rate_mbps == 54,
              "...and its rate STILL reads 54 Mbps, because HE MCS tables are not decoded (L7)");

        bn = make_beacon(bcn, bssid, "ZXV-NET", 6, SEC_BOTH, PHY_AC);
        CHECK(wifi_parse_beacon(bcn, bn, -60, 2437, &r) == WIFI_OK &&
                  r.security == WIFI_SEC_WPA2_WPA3,
              "PSK + SAE together read as WPA2/WPA3 mixed");
        CHECK(r.standard == WIFI_802_11AC, "the VHT capability element means 802.11ac");

        bn = make_beacon(bcn, bssid, "OldNet", 11, SEC_WPA1, PHY_B);
        CHECK(wifi_parse_beacon(bcn, bn, -70, 2462, &r) == WIFI_OK && r.security == WIFI_SEC_WPA,
              "vendor element 221 with OUI 00:50:F2 type 1 reads as WPA");
        CHECK(r.channel == 11 && r.max_rate_mbps == 11 && r.standard == WIFI_802_11B,
              "channel 11, 11 Mbps top rate, 802.11b");

        bn = make_beacon(bcn, bssid, "WepNet", 1, SEC_WEP, PHY_G);
        CHECK(wifi_parse_beacon(bcn, bn, -70, 2412, &r) == WIFI_OK && r.security == WIFI_SEC_WEP,
              "the Privacy capability bit with no RSN/WPA element reads as WEP");

        bn = make_beacon(bcn, bssid, "FreeWifi", 1, SEC_OPEN, PHY_G);
        CHECK(wifi_parse_beacon(bcn, bn, -70, 2412, &r) == WIFI_OK && r.security == WIFI_SEC_OPEN,
              "no Privacy bit and no security element reads as OPEN");

        bn = make_beacon(bcn, bssid, "", 1, SEC_OPEN, PHY_G);
        CHECK(wifi_parse_beacon(bcn, bn, -70, 2412, &r) == WIFI_OK && r.hidden && r.ssid[0] == '\0',
              "a zero-length SSID element marks the network hidden");

        /* 5 GHz: the frequency wins over a stale DS element */
        bn = make_beacon(bcn, bssid, "FiveG", 36, SEC_WPA2, PHY_AC);
        CHECK(wifi_parse_beacon(bcn, bn, -55, 5180, &r) == WIFI_OK && r.channel == 36 &&
                  r.band == WIFI_BAND_5GHZ,
              "5180 MHz gives channel 36, 5 GHz");
        {
            /* The two sources must be made to DISAGREE, or "the frequency
             * wins" is never actually exercised: in every case above the DS
             * param happens to say the same thing the radio measured. */
            uint32_t dn = make_beacon(bcn, bssid, "Liar", 1, SEC_OPEN, PHY_G);
            CHECK(wifi_parse_beacon(bcn, dn, -50, 2437, &r) == WIFI_OK && r.channel == 6,
                  "a beacon whose DS param claims channel 1 but which was HEARD on "
                  "2437 MHz is reported on channel 6 — the measurement wins");
            CHECK(wifi_parse_beacon(bcn, dn, -50, 0, &r) == WIFI_OK && r.channel == 1,
                  "...and with no measurement to go on, the same frame reports the "
                  "channel 1 its DS param claims");
            dn = make_beacon(bcn, bssid, "Liar5", 6, SEC_OPEN, PHY_AC);
            CHECK(wifi_parse_beacon(bcn, dn, -50, 5180, &r) == WIFI_OK && r.channel == 36 &&
                      r.band == WIFI_BAND_5GHZ,
                  "a DS param claiming 2.4 GHz channel 6 loses to a frame heard at "
                  "5180 MHz: channel 36, 5 GHz");
            CHECK(wifi_parse_beacon(bcn, dn, -50, 0, &r) == WIFI_OK && r.channel == 6 &&
                      r.band == WIFI_BAND_2_4GHZ,
                  "...and unmeasured, that same frame is taken at its word: channel 6, "
                  "2.4 GHz");
        }

        /* --- hostile input --- */
        bn = make_beacon(bcn, bssid, "ZXV-NET", 6, SEC_WPA2, PHY_G);
        CHECK(wifi_parse_beacon(bcn, 30, -47, 2437, &r) == WIFI_EINVAL,
              "a beacon truncated inside its fixed parameters is refused");
        CHECK(wifi_parse_beacon(bcn, 10, -47, 2437, &r) == WIFI_EINVAL,
              "a 10-byte 'beacon' is refused");
        {
            uint8_t evil[512];
            memcpy(evil, bcn, bn);
            evil[24 + 12 + 1] = 200; /* SSID element claims 200 bytes */
            CHECK(wifi_parse_beacon(evil, bn, -47, 2437, &r) == WIFI_EINVAL,
                  "an element whose length runs past the frame is refused, not clamped");
        }
        {
            uint8_t evil[512];
            memcpy(evil, bcn, bn);
            evil[0] = 0x40; /* subtype 4 = probe REQUEST */
            CHECK(wifi_parse_beacon(evil, bn, -47, 2437, &r) == WIFI_ENOTSUP,
                  "a probe request is not a beacon and is not parsed as one");
        }
        {
            uint8_t evil[512];
            memcpy(evil, bcn, bn);
            evil[0] = (uint8_t) (WIFI_STYPE_PROBE_RESP << 4);
            CHECK(wifi_parse_beacon(evil, bn, -47, 2437, &r) == WIFI_OK,
                  "a probe RESPONSE is parsed the same way as a beacon");
        }
        {
            const uint8_t *v = 0;
            uint8_t ies[6] = {0, 2, 'h', 'i', 3, 1}; /* last element is truncated */
            CHECK(wifi_find_ie(ies, 4, WIFI_EID_SSID, &v) == 2 && v[0] == 'h',
                  "wifi_find_ie returns the element length and a pointer to its body");
            CHECK(wifi_find_ie(ies, 6, WIFI_EID_DSPARAM, &v) == WIFI_EINVAL,
                  "an element list that ends mid-element is refused");
            CHECK(wifi_find_ie(ies, 4, 99, &v) == WIFI_EAGAIN,
                  "a well-formed list simply missing element 99 says so distinctly");
        }
    }

    /* ================================================================= */
    printf("\n--- scan result table ---\n");
    /* ================================================================= */
    {
        wifi_scan_clear(&dev);
        wifi_scan_result_t r;
        memset(&r, 0, sizeof r);
        strcpy(r.ssid, "Alpha");
        r.bssid[0] = 0xAA;
        r.bssid[5] = 0x01;
        r.rssi = -50;
        r.channel = 1;
        CHECK(wifi_scan_add_result(&dev, &r) == 0, "the first result lands at index 0");
        CHECK(wifi_get_scan_count(&dev) == 1, "the count is 1");

        r.rssi = -40;
        CHECK(wifi_scan_add_result(&dev, &r) == 0,
              "the same BSSID merges into index 0 rather than duplicating");
        CHECK(wifi_get_scan_count(&dev) == 1 && wifi_get_scan_result(&dev, 0)->rssi == -40,
              "...and the stronger RSSI -40 replaced -50");

        strcpy(r.ssid, "Alpha");
        r.bssid[5] = 0x02;
        r.rssi = -80;
        CHECK(wifi_scan_add_result(&dev, &r) == 1, "a second BSSID for the same SSID is separate");
        CHECK(wifi_scan_find_ssid(&dev, "Alpha") == 0,
              "find_ssid picks the STRONGEST of the two (-40 at index 0, not -80 at 1)");
        CHECK(wifi_scan_find_ssid(&dev, "Nope") == WIFI_EAGAIN, "an unknown SSID is not found");
        uint8_t probe[6] = {0xAA, 0, 0, 0, 0, 0x02};
        CHECK(wifi_scan_find_bssid(&dev, probe) == 1, "find_bssid locates index 1");

        memset(&r, 0, sizeof r);
        CHECK(wifi_scan_add_result(&dev, &r) == WIFI_EINVAL,
              "an all-zero BSSID is not a real AP and is refused");

        /* Hidden-SSID handling on merge. A real AP answers a directed probe
         * with its name but beacons with a zero-length SSID element, so the
         * same BSSID arrives named and unnamed alternately. Learning the name
         * must be sticky: a later nameless sighting must not erase it. */
        {
            wifi_scan_clear(&dev);
            wifi_scan_result_t named, blank;
            memset(&named, 0, sizeof named);
            named.bssid[0] = 0xBE;
            named.bssid[5] = 0x01;
            named.rssi = -55;
            named.channel = 6;
            strcpy(named.ssid, "SecretNet");

            blank = named;
            blank.ssid[0] = '\0';
            blank.hidden = true;
            blank.rssi = -60;

            CHECK(wifi_scan_add_result(&dev, &blank) == 0 &&
                      wifi_get_scan_result(&dev, 0)->hidden &&
                      wifi_get_scan_result(&dev, 0)->ssid[0] == '\0',
                  "a hidden beacon lands first with no name and hidden = true");
            CHECK(wifi_scan_add_result(&dev, &named) == 0 &&
                      strcmp(wifi_get_scan_result(&dev, 0)->ssid, "SecretNet") == 0 &&
                      !wifi_get_scan_result(&dev, 0)->hidden,
                  "a probe response carrying the real name replaces the hidden entry");
            CHECK(wifi_scan_add_result(&dev, &blank) == 0,
                  "the next nameless beacon merges into the same entry");
            CHECK(strcmp(wifi_get_scan_result(&dev, 0)->ssid, "SecretNet") == 0,
                  "...and the LEARNED NAME SURVIVES it — a blank sighting never erases a "
                  "name we already have");
            CHECK(!wifi_get_scan_result(&dev, 0)->hidden,
                  "...and the entry is no longer marked hidden, because we know its name");
            CHECK(wifi_get_scan_result(&dev, 0)->rssi == -60,
                  "...while everything else, like the RSSI, IS taken from the newer sighting");
            CHECK(wifi_scan_find_ssid(&dev, "SecretNet") == 0,
                  "and the network is findable by the name it never re-advertised");
            wifi_scan_clear(&dev);
        }

        /* rebuild the two-BSSID state the eviction test below expects */
        memset(&r, 0, sizeof r);
        strcpy(r.ssid, "Alpha");
        r.bssid[0] = 0xAA;
        r.bssid[5] = 0x01;
        r.rssi = -40;
        r.channel = 1;
        (void) wifi_scan_add_result(&dev, &r);
        r.bssid[5] = 0x02;
        r.rssi = -80;
        (void) wifi_scan_add_result(&dev, &r);
        CHECK(wifi_get_scan_count(&dev) == 2, "the table is back to two entries");

        /* fill the table, then test eviction */
        wifi_scan_clear(&dev);
        CHECK(wifi_get_scan_count(&dev) == 0, "clear empties the table");
        int fill_ok = 1;
        for (uint32_t i = 0; i < WIFI_MAX_SCAN_RESULTS; i++) {
            memset(&r, 0, sizeof r);
            r.bssid[0] = 0x02;
            r.bssid[5] = (uint8_t) i;
            r.rssi = (int16_t) (-40 - (int) i); /* index 63 is weakest at -103 */
            r.channel = 1;
            if (wifi_scan_add_result(&dev, &r) != (int) i) fill_ok = 0;
        }
        CHECK(fill_ok && wifi_get_scan_count(&dev) == WIFI_MAX_SCAN_RESULTS,
              "64 distinct BSSIDs fill the table exactly");
        memset(&r, 0, sizeof r);
        r.bssid[0] = 0x02;
        r.bssid[5] = 0xFE;
        r.rssi = -120;
        r.channel = 1;
        CHECK(wifi_scan_add_result(&dev, &r) == WIFI_ENOSPC,
              "a weaker AP than everything present is dropped with ENOSPC");
        r.rssi = -10;
        CHECK(wifi_scan_add_result(&dev, &r) == 63,
              "a stronger AP evicts the weakest entry (index 63, which was -103)");
        CHECK(wifi_get_scan_result(&dev, 63)->rssi == -10, "...and index 63 now reads -10 dBm");
        CHECK(wifi_get_scan_result(&dev, 64) == 0, "index 64 is out of range -> NULL");
    }

    /* ================================================================= */
    printf("\n--- association state machine ---\n");
    /* ================================================================= */
    {
        /* The full legal-edge set, spelled out here independently of wifi.c. */
        static const int legal[WIFI_STATE__COUNT][WIFI_STATE__COUNT] = {
            /* from\to      IDLE SCAN AUTH ASSOC HS  CONN BCN FAIL */
            /* IDLE   */ {0, 1, 1, 0, 0, 0, 1, 0},
            /* SCAN   */ {1, 0, 0, 0, 0, 0, 0, 1},
            /* AUTH   */ {1, 0, 0, 1, 0, 0, 0, 1},
            /* ASSOC  */ {1, 0, 0, 0, 1, 1, 0, 1},
            /* HS     */ {1, 0, 0, 0, 0, 1, 0, 1},
            /* CONN   */ {1, 0, 0, 0, 0, 0, 0, 1},
            /* BCN    */ {1, 0, 0, 0, 0, 0, 0, 1},
            /* FAIL   */ {1, 0, 0, 0, 0, 0, 0, 0},
        };
        int mismatches = 0, legal_count = 0;
        for (int a = 0; a < WIFI_STATE__COUNT; a++)
            for (int b = 0; b < WIFI_STATE__COUNT; b++) {
                bool got =
                    wifi_state_can_transition((wifi_assoc_state_t) a, (wifi_assoc_state_t) b);
                if (got != (legal[a][b] != 0)) mismatches++;
                if (legal[a][b]) legal_count++;
            }
        printf("       %d of %d transitions are legal (%d mismatches)\n", legal_count,
               WIFI_STATE__COUNT * WIFI_STATE__COUNT, mismatches);
        CHECK(mismatches == 0 && legal_count == 20,
              "all 64 state pairs match the transition table: exactly 20 edges are legal");
        CHECK(!wifi_state_can_transition(WIFI_STATE_IDLE, WIFI_STATE_IDLE),
              "IDLE->IDLE is refused — a no-op 'transition' hides bugs");
        CHECK(!wifi_state_can_transition(WIFI_STATE_IDLE, WIFI_STATE_CONNECTED),
              "IDLE->CONNECTED is refused: you cannot connect without authenticating");
        CHECK(!wifi_state_can_transition(WIFI_STATE_CONNECTED, WIFI_STATE_AUTHENTICATING),
              "CONNECTED->AUTHENTICATING is refused");
        CHECK(!wifi_state_can_transition((wifi_assoc_state_t) 99, WIFI_STATE_IDLE),
              "an out-of-range state is refused rather than indexing the table");

        /* the mode rule, which the pure predicate cannot see */
        wifi_device_t d2;
        wifi_init(&d2, "modes");
        uint32_t s = wifi_create_interface(&d2, "sta", WIFI_MODE_STATION);
        uint32_t a = wifi_create_interface(&d2, "ap", WIFI_MODE_AP);
        CHECK(wifi_state_set(&d2, s, WIFI_STATE_BEACONING) == WIFI_ESTATE,
              "a STATION cannot enter BEACONING");
        CHECK(wifi_state_set(&d2, a, WIFI_STATE_AUTHENTICATING) == WIFI_OK &&
                  wifi_state_set(&d2, a, WIFI_STATE_ASSOCIATING) == WIFI_OK &&
                  wifi_state_set(&d2, a, WIFI_STATE_CONNECTED) == WIFI_ESTATE,
              "an AP interface cannot enter CONNECTED");
        CHECK(wifi_state_set(&d2, s, WIFI_STATE_SCANNING) == WIFI_OK &&
                  wifi_get_interface(&d2, s)->connected == false,
              "SCANNING does not set the connected flag");
        CHECK(wifi_state_set(&d2, s, WIFI_STATE_AUTHENTICATING) == WIFI_ESTATE,
              "SCANNING->AUTHENTICATING is refused (abort the scan first)");
    }

    /* ================================================================= */
    printf("\n--- SHA-1 (FIPS 180-1) ---\n");
    /* ================================================================= */
    {
        uint8_t d[20];
        wifi_sha1((const uint8_t *) "abc", 3, d);
        dump("SHA1(\"abc\")", d, 20);
        CHECK(hexeq(d, "a9993e364706816aba3e25717850c26c9cd0d89d", 20),
              "SHA-1(\"abc\") matches the FIPS 180-1 vector");
        wifi_sha1((const uint8_t *) "", 0, d);
        CHECK(hexeq(d, "da39a3ee5e6b4b0d3255bfef95601890afd80709", 20), "SHA-1(\"\") matches");
        const char *m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        wifi_sha1((const uint8_t *) m2, (uint32_t) strlen(m2), d);
        CHECK(hexeq(d, "84983e441c3bd26ebaae4aa1f95129e5e54670f1", 20),
              "SHA-1 of the 448-bit FIPS message matches (two-block padding path)");
        { /* one million 'a' — the streaming path with no staging buffer */
            wifi_sha1_ctx_t c;
            wifi_sha1_init(&c);
            uint8_t chunk[1000];
            memset(chunk, 'a', sizeof chunk);
            for (int i = 0; i < 1000; i++) wifi_sha1_update(&c, chunk, sizeof chunk);
            wifi_sha1_final(&c, d);
            CHECK(hexeq(d, "34aa973cd4c4daa4f61eeb2bdbad27316534016f", 20),
                  "SHA-1 of 1,000,000 'a' matches — streaming covers the WHOLE message");
        }
        { /* odd-sized updates must equal the one-shot digest */
            const char *msg = "The quick brown fox jumps over the lazy dog";
            uint8_t one[20], many[20];
            wifi_sha1((const uint8_t *) msg, (uint32_t) strlen(msg), one);
            wifi_sha1_ctx_t c;
            wifi_sha1_init(&c);
            uint32_t off = 0, len = (uint32_t) strlen(msg);
            while (off < len) {
                uint32_t take = 1 + (rnd() % 7);
                if (off + take > len) take = len - off;
                wifi_sha1_update(&c, (const uint8_t *) msg + off, take);
                off += take;
            }
            wifi_sha1_final(&c, many);
            CHECK(memcmp(one, many, 20) == 0,
                  "streaming in ragged 1..7 byte chunks equals the one-shot digest");
        }
    }

    /* ================================================================= */
    printf("\n--- HMAC-SHA1 (RFC 2202) ---\n");
    /* ================================================================= */
    {
        uint8_t d[20], key[80], data[50];
        memset(key, 0x0b, 20);
        wifi_hmac_sha1(key, 20, (const uint8_t *) "Hi There", 8, d);
        CHECK(hexeq(d, "b617318655057264e28bc0b6fb378c8ef146be00", 20),
              "RFC 2202 case 1: HMAC-SHA1(0x0b*20, \"Hi There\")");
        wifi_hmac_sha1((const uint8_t *) "Jefe", 4,
                       (const uint8_t *) "what do ya want for nothing?", 28, d);
        CHECK(hexeq(d, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79", 20),
              "RFC 2202 case 2: HMAC-SHA1(\"Jefe\", ...)");
        memset(key, 0xaa, 20);
        memset(data, 0xdd, 50);
        wifi_hmac_sha1(key, 20, data, 50, d);
        CHECK(hexeq(d, "125d7342b9ac11cd91a39af48aa17b4f63f175d3", 20),
              "RFC 2202 case 3: HMAC-SHA1(0xaa*20, 0xdd*50)");
        memset(key, 0xaa, 80);
        wifi_hmac_sha1(key, 80,
                       (const uint8_t *) "Test Using Larger Than Block-Size Key - Hash Key First",
                       54, d);
        CHECK(hexeq(d, "aa4ae5e15272d00e95705637ce8a3b55ed402112", 20),
              "RFC 2202 case 6: an 80-byte key is hashed down first");
    }

    /* ================================================================= */
    printf("\n--- PBKDF2-HMAC-SHA1 (RFC 6070) ---\n");
    /* ================================================================= */
    {
        uint8_t dk[32];
        CHECK(wifi_pbkdf2_sha1((const uint8_t *) "password", 8, (const uint8_t *) "salt", 4, 1, dk,
                               20) == WIFI_OK &&
                  hexeq(dk, "0c60c80f961f0e71f3a9b524af6012062fe037a6", 20),
              "RFC 6070 #1: c=1, dkLen=20");
        CHECK(wifi_pbkdf2_sha1((const uint8_t *) "password", 8, (const uint8_t *) "salt", 4, 2, dk,
                               20) == WIFI_OK &&
                  hexeq(dk, "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957", 20),
              "RFC 6070 #2: c=2, dkLen=20");
        CHECK(wifi_pbkdf2_sha1((const uint8_t *) "password", 8, (const uint8_t *) "salt", 4, 4096,
                               dk, 20) == WIFI_OK &&
                  hexeq(dk, "4b007901b765489abead49d926f721d065a429c1", 20),
              "RFC 6070 #3: c=4096, dkLen=20");
        CHECK(wifi_pbkdf2_sha1((const uint8_t *) "passwordPASSWORDpassword", 24,
                               (const uint8_t *) "saltSALTsaltSALTsaltSALTsaltSALTsalt", 36, 4096,
                               dk, 25) == WIFI_OK &&
                  hexeq(dk, "3d2eec4fe41c849b80c8d83662c0e44a8b291a964cf2f07038", 25),
              "RFC 6070 #5: dkLen=25 spans two blocks");
        CHECK(wifi_pbkdf2_sha1((const uint8_t *) "pass\0word", 9, (const uint8_t *) "sa\0lt", 5,
                               4096, dk, 16) == WIFI_OK &&
                  hexeq(dk, "56fa6aa75548099dcc37d7f03425e0c3", 16),
              "RFC 6070 #6: embedded NUL bytes are hashed, not treated as terminators");
        { /* the same property, proved without reference to any table: a NUL
           * inside the password or the salt must change the answer. */
            uint8_t a[16], b[16];
            (void) wifi_pbkdf2_sha1((const uint8_t *) "pass\0word", 9, (const uint8_t *) "sa\0lt",
                                    5, 64, a, 16);
            (void) wifi_pbkdf2_sha1((const uint8_t *) "pass", 4, (const uint8_t *) "sa\0lt", 5, 64,
                                    b, 16);
            CHECK(memcmp(a, b, 16) != 0,
                  "\"pass\\0word\"(9) and \"pass\"(4) derive DIFFERENT keys — the NUL is data");
            (void) wifi_pbkdf2_sha1((const uint8_t *) "pass\0word", 9, (const uint8_t *) "sa", 2,
                                    64, b, 16);
            CHECK(memcmp(a, b, 16) != 0,
                  "a salt truncated at its NUL also derives a different key");
        }
        CHECK(wifi_pbkdf2_sha1((const uint8_t *) "p", 1, (const uint8_t *) "s", 1, 0, dk, 20) ==
                  WIFI_EINVAL,
              "zero iterations is refused");
        CHECK(wifi_pbkdf2_sha1((const uint8_t *) "p", 1, (const uint8_t *) "s", 1, 1, dk, 0) ==
                  WIFI_EINVAL,
              "a zero-length output is refused");
    }

    /* ================================================================= */
    printf("\n--- WPA2 PMK (IEEE 802.11i Annex H.4 passphrase-to-PSK) ---\n");
    /* ================================================================= */
    uint8_t pmk[32];
    {
        CHECK(wifi_wpa_pmk("password", "IEEE", pmk) == WIFI_OK, "PMK derives");
        dump("PMK(\"password\",\"IEEE\")", pmk, 32);
        CHECK(hexeq(pmk, "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e", 32),
              "802.11i H.4 #1: passphrase \"password\", SSID \"IEEE\"");
        CHECK(
            wifi_wpa_pmk("ThisIsAPassword", "ThisIsASSID", pmk) == WIFI_OK &&
                hexeq(pmk, "0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af", 32),
            "802.11i H.4 #2: \"ThisIsAPassword\" / \"ThisIsASSID\"");
        {
            char p64[65], s32[33];
            memset(p64, 'a', 64);
            p64[64] = '\0';
            memset(s32, 'Z', 32);
            s32[32] = '\0';
            CHECK(wifi_wpa_pmk(p64, s32, pmk) == WIFI_EINVAL,
                  "a 64-character passphrase is refused: 802.11i allows 8..63, and 64 "
                  "characters is a raw hex PSK, not a passphrase");
            /* NOTE: 802.11i Annex H.4 has a third vector using a 64-character
             * passphrase, i.e. one octet past the range the same standard
             * specifies. It is deliberately NOT asserted here: this author
             * could not confirm its expected PSK from a source he trusts, and
             * asserting a value produced by the code under test would prove
             * nothing. The two H.4 vectors above and the four RFC 6070
             * vectors already pin PBKDF2-HMAC-SHA1 byte-for-byte.
             * What IS checked here is that nothing is silently truncated at
             * the boundary: 63 and 62 characters must differ. */
            uint8_t k63[32], k62[32];
            p64[63] = '\0';
            CHECK(wifi_wpa_pmk(p64, s32, k63) == WIFI_OK, "63 'a' is accepted");
            p64[62] = '\0';
            CHECK(wifi_wpa_pmk(p64, s32, k62) == WIFI_OK && memcmp(k63, k62, 32) != 0,
                  "62 and 63 characters give different PMKs — no silent truncation");
        }
        CHECK(wifi_wpa_pmk("short", "ssid", pmk) == WIFI_EINVAL, "a 5-char passphrase is refused");
        CHECK(wifi_wpa_pmk("password", "", pmk) == WIFI_EINVAL, "an empty SSID is refused");
    }

    /* ================================================================= */
    printf("\n--- PTK derivation (802.11i PRF-384) ---\n");
    /* ================================================================= */
    uint8_t anonce[32], snonce[32];
    uint8_t AA[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t SPA[6] = {0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};
    wifi_ptk_t ptk, ptk2;
    {
        for (int i = 0; i < 32; i++) {
            anonce[i] = (uint8_t) (0xA0 + i);
            snonce[i] = (uint8_t) i;
        }
        (void) wifi_wpa_pmk("correcthorse", "ZXV-NET", pmk);

        CHECK(wifi_wpa_derive_ptk(pmk, AA, SPA, anonce, snonce, &ptk) == WIFI_OK,
              "PTK derives from PMK, both MACs and both nonces");
        dump("KCK", ptk.kck, 16);
        dump("TK ", ptk.tk, 16);
        CHECK(memcmp(ptk.kck, ptk.kek, 16) != 0 && memcmp(ptk.kek, ptk.tk, 16) != 0 &&
                  memcmp(ptk.kck, ptk.tk, 16) != 0,
              "KCK, KEK and TK are three DIFFERENT 16-byte keys, not the same block repeated");
        CHECK(wifi_wpa_derive_ptk(pmk, AA, SPA, anonce, snonce, &ptk2) == WIFI_OK &&
                  memcmp(&ptk, &ptk2, sizeof ptk) == 0,
              "derivation is deterministic");
        CHECK(wifi_wpa_derive_ptk(pmk, SPA, AA, snonce, anonce, &ptk2) == WIFI_OK &&
                  memcmp(&ptk, &ptk2, sizeof ptk) == 0,
              "swapping (AA,SPA) and (ANonce,SNonce) gives the SAME PTK — Min/Max ordering "
              "is what lets both ends agree");
        {
            uint8_t n2[32];
            memcpy(n2, snonce, 32);
            n2[31] ^= 0x01;
            CHECK(wifi_wpa_derive_ptk(pmk, AA, SPA, anonce, n2, &ptk2) == WIFI_OK &&
                      memcmp(&ptk, &ptk2, sizeof ptk) != 0,
                  "flipping ONE bit of the SNonce changes the PTK");
            uint8_t m2[6];
            memcpy(m2, SPA, 6);
            m2[5] ^= 0x01;
            CHECK(wifi_wpa_derive_ptk(pmk, AA, m2, anonce, snonce, &ptk2) == WIFI_OK &&
                      memcmp(&ptk, &ptk2, sizeof ptk) != 0,
                  "flipping one bit of the supplicant MAC changes the PTK");
            uint8_t p2[32];
            memcpy(p2, pmk, 32);
            p2[0] ^= 0x01;
            CHECK(wifi_wpa_derive_ptk(p2, AA, SPA, anonce, snonce, &ptk2) == WIFI_OK &&
                      memcmp(&ptk, &ptk2, sizeof ptk) != 0,
                  "flipping one bit of the PMK changes the PTK");
        }
        { /* the PRF's own framing: label || 0x00 || data || counter */
            uint8_t out[40], want[40], blk[20];
            uint8_t key[16];
            memset(key, 0x5a, 16);
            uint8_t data[4] = {1, 2, 3, 4};
            CHECK(wifi_wpa_prf(key, 16, "lbl", data, 4, out, 40) == WIFI_OK, "PRF-320 runs");
            for (uint8_t i = 0; i < 2; i++) {
                uint8_t msg[3 + 1 + 4 + 1];
                memcpy(msg, "lbl", 3);
                msg[3] = 0;
                memcpy(msg + 4, data, 4);
                msg[8] = i;
                wifi_hmac_sha1(key, 16, msg, sizeof msg, blk);
                memcpy(want + i * 20, blk, 20);
            }
            CHECK(memcmp(out, want, 40) == 0,
                  "PRF output equals HMAC-SHA1(K, label||0x00||data||i) concatenated for i=0,1");
            CHECK(wifi_wpa_prf(key, 16, "lbl", data, 4, out, 0) == WIFI_EINVAL,
                  "a zero-length PRF output is refused");
        }
    }

    /* ================================================================= */
    printf("\n--- EAPOL-Key parsing, building and the MIC ---\n");
    /* ================================================================= */
    {
        uint8_t frame[256];
        wifi_eapol_key_t k;
        uint32_t n = make_eapol(frame, (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_ACK),
                                0x0102030405060708ull, anonce, 0, 0, 0);
        CHECK(n == 99, "an EAPOL-Key frame with no key data is 99 bytes (4 + 95)");
        CHECK(wifi_eapol_key_parse(frame, n, &k) == WIFI_OK, "it parses");
        CHECK(k.descriptor_type == 2, "descriptor type 2 (RSN) at offset 4");
        CHECK((k.key_info & WIFI_KI_VERSION_MASK) == 2,
              "key descriptor version 2 (HMAC-SHA1-128) in key info bits 0-2");
        CHECK(k.replay_counter == 0x0102030405060708ull,
              "the 8-byte replay counter at offset 9 is BIG endian");
        CHECK(memcmp(k.nonce, anonce, 32) == 0, "the 32-byte nonce sits at offset 17");
        CHECK(k.key_data_len == 0 && k.total_len == 99, "key data length 0 at offset 97");
        CHECK(k.msg == 1, "ACK set and MIC clear classifies as message 1");

        n = make_eapol(frame, (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_MIC), 1, snonce,
                       (const uint8_t *) "\x30\x14", 2, 0);
        CHECK(wifi_eapol_key_parse(frame, n, &k) == WIFI_OK && k.msg == 2 && k.key_data_len == 2 &&
                  k.total_len == 101,
              "MIC set, ACK clear, SECURE clear classifies as message 2 (101 bytes)");
        n = make_eapol(frame,
                       (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_MIC | WIFI_KI_ACK |
                                   WIFI_KI_SECURE | WIFI_KI_INSTALL),
                       2, anonce, 0, 0, 0);
        CHECK(wifi_eapol_key_parse(frame, n, &k) == WIFI_OK && k.msg == 3,
              "ACK and MIC together classify as message 3");
        n = make_eapol(frame, (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_MIC | WIFI_KI_SECURE), 2,
                       0, 0, 0, 0);
        CHECK(wifi_eapol_key_parse(frame, n, &k) == WIFI_OK && k.msg == 4,
              "MIC and SECURE without ACK classify as message 4");
        n = make_eapol(frame, (uint16_t) (2 | WIFI_KI_MIC | WIFI_KI_SECURE), 3, 0, 0, 0, 0);
        CHECK(wifi_eapol_key_parse(frame, n, &k) == WIFI_OK && k.msg == 0,
              "a GROUP-key frame (no Pairwise bit) is not mistaken for a pairwise message");

        /* malformed */
        n = make_eapol(frame, (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_ACK), 1, anonce, 0, 0, 0);
        CHECK(wifi_eapol_key_parse(frame, 98, &k) == WIFI_EINVAL,
              "98 bytes is one short of the fixed part and is refused");
        frame[1] = 0; /* EAPOL type "EAP-Packet", not Key */
        CHECK(wifi_eapol_key_parse(frame, n, &k) == WIFI_EINVAL, "a non-Key EAPOL type is refused");
        frame[1] = 3;
        frame[98] = 40; /* claim 40 bytes of key data that are not there */
        CHECK(wifi_eapol_key_parse(frame, n, &k) == WIFI_EINVAL,
              "a key-data length that runs past the buffer is refused");
        n = make_eapol(frame, (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_ACK), 1, anonce, 0, 0, 0);
        frame[3] = 90; /* body length below the fixed minimum */
        CHECK(wifi_eapol_key_parse(frame, n, &k) == WIFI_EINVAL,
              "a body length of 90 is below the 95-byte fixed part and is refused");
        /* The case above is caught by the MINIMUM-body test, not by the
         * body-vs-key-data consistency test. To reach that one the body must
         * be large enough and inside the buffer, and only DISAGREE with the
         * declared key-data length. Both directions of the disagreement: */
        {
            uint8_t kd[10];
            memset(kd, 0xA5, sizeof kd);
            uint32_t tn = make_eapol(frame, (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_ACK), 1,
                                     anonce, kd, 10, 0);
            CHECK(tn == 109 && wifi_eapol_key_parse(frame, tn, &k) == WIFI_OK &&
                      k.key_data_len == 10 && k.total_len == 109,
                  "a consistent 109-byte frame (99 + 10 key data) parses");

            frame[98] = 5; /* body still says 105; key data now claims 5 */
            CHECK(wifi_eapol_key_parse(frame, tn, &k) == WIFI_EINVAL,
                  "body 105 with key-data 5 disagree (109 != 104) and are refused — this is "
                  "the consistency check itself, not the minimum-length check");
            frame[98] = 10;
            frame[2] = 0;
            frame[3] = 100; /* body 100; key data still 10 */
            CHECK(wifi_eapol_key_parse(frame, tn, &k) == WIFI_EINVAL,
                  "body 100 with key-data 10 disagree the other way (104 != 109) and are refused");
            frame[3] = 105;
            CHECK(wifi_eapol_key_parse(frame, tn, &k) == WIFI_OK,
                  "restoring body 105 makes the same bytes parse again");
        }

        /* --- the MIC, and making it fail --- */
        uint8_t m2buf[256];
        wifi_eapol_key_t m2;
        memset(&m2, 0, sizeof m2);
        m2.descriptor_type = 2;
        m2.key_info = (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_MIC);
        m2.replay_counter = 7;
        memcpy(m2.nonce, snonce, 32);
        int bn = wifi_eapol_key_build(&m2, 0, m2buf, sizeof m2buf);
        CHECK(bn == 99, "building an M2 with no key data gives 99 bytes");
        CHECK(wifi_eapol_key_build(&m2, 0, m2buf, 98) == WIFI_EMSGSIZE,
              "building into 98 bytes is refused — one byte short is still short");
        {
            uint8_t kd22[22];
            wifi_eapol_key_t big = m2;
            memset(kd22, 0x5A, sizeof kd22);
            big.key_data_len = 22;
            CHECK(wifi_eapol_key_build(&big, kd22, m2buf, 120) == WIFI_EMSGSIZE,
                  "a 121-byte frame does not fit in 120 bytes and is refused, not truncated");
            CHECK(wifi_eapol_key_build(&big, kd22, m2buf, 121) == 121,
                  "...and fits exactly in 121");
            CHECK(m2buf[97] == 0 && m2buf[98] == 22 && m2buf[99] == 0x5A && m2buf[120] == 0x5A,
                  "...with the key data written at offset 99 through 120");
            CHECK(wifi_eapol_key_build(&big, 0, m2buf, sizeof m2buf) == WIFI_EINVAL,
                  "claiming 22 bytes of key data while passing NULL is refused");
            /* rebuild the plain M2 the MIC checks below expect */
            CHECK(wifi_eapol_key_build(&m2, 0, m2buf, sizeof m2buf) == 99,
                  "the plain 99-byte M2 is rebuilt for the MIC checks");
        }
        CHECK(m2buf[0] == 2 && m2buf[1] == 3, "802.1X version 2, type 3 (EAPOL-Key)");
        CHECK(m2buf[2] == 0 && m2buf[3] == 95, "the body length field reads 95");
        CHECK(m2buf[16] == 7, "the replay counter's low byte lands at offset 16");

        uint8_t mic[16];
        CHECK(wifi_eapol_mic(ptk.kck, m2buf, (uint32_t) bn, 2, mic) == WIFI_OK, "the MIC computes");
        memcpy(m2buf + WIFI_EAPOL_MIC_OFF, mic, 16);
        CHECK(wifi_eapol_mic_verify(ptk.kck, m2buf, (uint32_t) bn, 2),
              "a frame carrying its own MIC verifies");
        { /* the module streams the HMAC in three pieces; do it the naive way */
            uint8_t copy[256], want[20];
            memcpy(copy, m2buf, (uint32_t) bn);
            memset(copy + WIFI_EAPOL_MIC_OFF, 0, 16);
            wifi_hmac_sha1(ptk.kck, 16, copy, (uint32_t) bn, want);
            CHECK(memcmp(want, mic, 16) == 0,
                  "the streamed MIC equals a single-shot HMAC over the zeroed-MIC frame");
        }
        /* --- and now make it FAIL, four different ways --- */
        m2buf[20] ^= 0x01;
        CHECK(!wifi_eapol_mic_verify(ptk.kck, m2buf, (uint32_t) bn, 2),
              "flipping one bit in the replay counter BREAKS the MIC");
        m2buf[20] ^= 0x01;
        m2buf[98] ^= 0x01;
        CHECK(!wifi_eapol_mic_verify(ptk.kck, m2buf, (uint32_t) bn, 2),
              "flipping one bit in the LAST byte breaks it too — the tail is covered");
        m2buf[98] ^= 0x01;
        m2buf[WIFI_EAPOL_MIC_OFF] ^= 0x80;
        CHECK(!wifi_eapol_mic_verify(ptk.kck, m2buf, (uint32_t) bn, 2),
              "corrupting the MIC field itself fails verification");
        m2buf[WIFI_EAPOL_MIC_OFF] ^= 0x80;
        {
            uint8_t wrong[16];
            memcpy(wrong, ptk.kck, 16);
            wrong[0] ^= 0x01;
            CHECK(!wifi_eapol_mic_verify(wrong, m2buf, (uint32_t) bn, 2),
                  "the wrong KCK fails verification");
        }
        CHECK(wifi_eapol_mic_verify(ptk.kck, m2buf, (uint32_t) bn, 2),
              "and it verifies again once everything is restored");
        CHECK(wifi_eapol_mic(ptk.kck, m2buf, (uint32_t) bn, 1, mic) == WIFI_ENOTSUP,
              "key descriptor version 1 (HMAC-MD5) is ENOTSUP — there is no MD5 here");
        CHECK(wifi_eapol_mic(ptk.kck, m2buf, (uint32_t) bn, 3, mic) == WIFI_ENOTSUP,
              "version 3 (AES-128-CMAC) is ENOTSUP — no AES-CMAC either");
        CHECK(!wifi_eapol_mic_verify(ptk.kck, m2buf, (uint32_t) bn, 3),
              "and a MIC we cannot compute NEVER verifies as valid");
    }

    /* ================================================================= */
    printf("\n--- the staging rings ---\n");
    /* ================================================================= */
    {
        wifi_device_t rd;
        wifi_init(&rd, "rings");
        uint32_t rsta = wifi_create_interface(&rd, "w", WIFI_MODE_STATION);
        CHECK(wifi_tx_pending(&rd) == 0 && wifi_rx_pending(&rd) == 0, "rings start empty");
        CHECK(wifi_tx_enqueue(&rd, "abc", 3) == WIFI_OK && wifi_tx_pending(&rd) == 5,
              "a 3-byte record occupies 5 bytes (2-byte length prefix)");
        CHECK(wifi_tx_flush(&rd, rsta) == WIFI_ENODEV,
              "flushing to a radio that is not there is ENODEV, not a silent success");
        CHECK(wifi_tx_pending(&rd) == 5, "...and the record is still queued");
        CHECK(wifi_get_interface(&rd, rsta)->tx_packets == 0,
              "...and enqueueing did NOT count as a transmit");

        /* wrap the ring many times and check every byte comes back */
        rng_state = 0xC0FFEEu;
        uint8_t src[600], dst[600];
        int mismatch = 0, cycles = 0;
        uint64_t bytes = 0;
        for (int i = 0; i < 20000; i++) {
            uint32_t len = 1 + (rnd() % 600);
            for (uint32_t j = 0; j < len; j++) src[j] = (uint8_t) (rnd());
            if (wifi_rx_inject(&rd, rsta, src, len) != WIFI_OK) {
                mismatch = 1;
                break;
            }
            int got = wifi_rx_packet(&rd, rsta, dst, sizeof dst);
            if (got != (int) len || memcmp(src, dst, len) != 0) {
                mismatch = 1;
                break;
            }
            bytes += len;
            cycles++;
        }
        printf("       %d inject/deliver cycles, %llu bytes through a 4096-byte ring\n", cycles,
               (unsigned long long) bytes);
        CHECK(!mismatch && cycles == 20000,
              "20,000 random-length records survive the ring byte-for-byte across wraparound");
        CHECK(wifi_rx_pending(&rd) == 0, "the ring is empty again afterwards");
        CHECK(wifi_get_interface(&rd, rsta)->rx_bytes == bytes,
              "rx_bytes equals exactly the number of bytes that really arrived");
        CHECK(wifi_get_interface(&rd, rsta)->rx_packets == 20000,
              "rx_packets counts each arrival once — delivery does not double-count");

        /* overflow and short-buffer behaviour */
        uint8_t big[2000];
        memset(big, 0x5A, sizeof big);
        int pushed = 0;
        /* Bounded on purpose: a 4096-byte ring cannot hold more than four of
         * these, so if the ENOSPC guard ever went missing an unbounded loop
         * would HANG here instead of failing, and a hang is a much worse
         * signal than a failed assertion. */
        while (pushed < 64 && wifi_rx_inject(&rd, rsta, big, 1000) == WIFI_OK) pushed++;
        CHECK(pushed == 4, "a 4096-byte ring holds exactly four 1000-byte records (1002 each)");
        CHECK(wifi_rx_inject(&rd, rsta, big, 1000) == WIFI_ENOSPC,
              "the fifth is refused with ENOSPC rather than overwriting the first");
        CHECK(wifi_get_interface(&rd, rsta)->rx_dropped == 2,
              "both refused injects (the loop's last one and this one) counted as drops");
        CHECK(wifi_get_interface(&rd, rsta)->rx_packets == 20004,
              "...and rx_packets counted only the 4 that actually fit");
        uint8_t small[10];
        CHECK(wifi_rx_packet(&rd, rsta, small, sizeof small) == WIFI_EMSGSIZE,
              "delivering a 1000-byte frame into a 10-byte buffer is refused");
        CHECK(wifi_rx_pending(&rd) == 4008, "...and nothing was consumed by the refused read");
        CHECK(wifi_rx_packet(&rd, rsta, big, sizeof big) == 1000,
              "a big enough buffer gets the whole 1000-byte frame");
        CHECK(wifi_tx_enqueue(&rd, big, 0) == WIFI_EINVAL, "a zero-length record is refused");
        CHECK(wifi_tx_enqueue(&rd, big, 3000) == WIFI_EINVAL,
              "a record larger than the maximum MPDU is refused");
    }

    /* ================================================================= */
    printf("\n--- the full STA association walk, over a virtual radio ---\n");
    /* ================================================================= */
    wifi_ops_t ops = vradio_ops_for(&vr);
    uint8_t our_mac[6];
    {
        memset(&vr, 0, sizeof vr);
        vr.have_random = 1;
        vr.random_seed = 0x40;
        wifi_init(&dev, "zxv-wifi0");
        sta = wifi_create_interface(&dev, "wlan0", WIFI_MODE_STATION);
        f = wifi_get_interface(&dev, sta);
        CHECK(wifi_bind_ops(&dev, &ops) == WIFI_OK && wifi_has_radio(&dev),
              "a radio backend binds");
        memcpy(our_mac, f->mac, 6);

        /* scan */
        CHECK(wifi_scan(&dev, sta) == WIFI_OK && vr.scan_calls == 1,
              "wifi_scan reaches the backend exactly once");
        CHECK(f->state == WIFI_STATE_SCANNING && dev.scanning, "the interface is SCANNING");
        CHECK(wifi_scan(&dev, sta) == WIFI_EBUSY, "a second scan while scanning is EBUSY");
        CHECK(wifi_get_scan_count(&dev) == 0,
              "a scan in flight has still produced NO results of its own");

        /* the radio hears one beacon */
        uint8_t bssid[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        uint8_t bcn[512];
        uint32_t bn = make_beacon(bcn, bssid, "ZXV-NET", 6, SEC_WPA2, PHY_N);
        wifi_scan_result_t r;
        CHECK(wifi_parse_beacon(bcn, bn, -42, 2437, &r) == WIFI_OK &&
                  wifi_scan_add_result(&dev, &r) == 0,
              "the heard beacon becomes scan result 0");
        CHECK(wifi_scan_complete(&dev, sta) == WIFI_OK && f->state == WIFI_STATE_IDLE,
              "the scan completes and the interface returns to IDLE");
        CHECK(wifi_scan_complete(&dev, sta) == WIFI_ESTATE,
              "completing a scan that is not running is ESTATE — an ISR cannot invent the "
              "end of a scan that never started");
        CHECK(wifi_scan_complete(&dev, 99) == WIFI_EINVAL,
              "...and completing a scan on an interface that does not exist is EINVAL");
        CHECK(wifi_get_scan_count(&dev) == 1, "exactly one AP was found — the one we heard");

        /* A beacon arriving through the ISR path must record the signal
         * strength the DRIVER measured for that frame. The interface's own
         * f->rssi belongs to a different BSS (here it is still the -127
         * "never measured" sentinel), so if that number ever shows up in a
         * scan result, the result is reporting a measurement nobody took. */
        {
            uint8_t obssid[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
            uint8_t obcn[512];
            uint32_t on = make_beacon(obcn, obssid, "Neighbour", 6, SEC_OPEN, PHY_G);
            CHECK(f->rssi == -127, "the interface itself has no RSSI measurement yet");
            CHECK(wifi_rx_mgmt(&dev, sta, obcn, on, -73) == WIFI_STYPE_BEACON,
                  "a beacon from a neighbouring AP is filed through the ISR path");
            int oi = wifi_scan_find_bssid(&dev, obssid);
            CHECK(oi >= 0, "...and lands in the scan table");
            CHECK(oi >= 0 && wifi_get_scan_result(&dev, (uint32_t) oi)->rssi == -73,
                  "...carrying the -73 dBm the DRIVER measured for that frame, not the "
                  "interface's own -127 sentinel");
            CHECK(f->rssi == -127,
                  "...and hearing a stranger did not overwrite our own link measurement");
            /* leave the table as the rest of this walk expects */
            {
                wifi_scan_result_t keep = *wifi_get_scan_result(&dev, 0);
                if (oi == 0) keep = *wifi_get_scan_result(&dev, 1);
                wifi_scan_clear(&dev);
                (void) wifi_scan_add_result(&dev, &keep);
            }
            CHECK(wifi_get_scan_count(&dev) == 1 &&
                      strcmp(wifi_get_scan_result(&dev, 0)->ssid, "ZXV-NET") == 0,
                  "the table is back to just the AP we are about to join");
        }

        /* connect: wrong credentials for the advertised suite */
        CHECK(wifi_connect(&dev, sta, "ZXV-NET", 0, WIFI_SEC_OPEN) == WIFI_EAUTH,
              "joining a WPA2 network as OPEN is refused (EAUTH), not attempted");
        CHECK(wifi_connect(&dev, sta, "NoSuchNet", "password1", WIFI_SEC_WPA2) == WIFI_EAGAIN,
              "connecting to an SSID we never heard says so (EAGAIN: scan first)");

        vr.tx_count = 0;
        CHECK(wifi_connect(&dev, sta, "ZXV-NET", "correcthorse", WIFI_SEC_WPA2) == WIFI_OK,
              "connect starts");
        CHECK(f->state == WIFI_STATE_AUTHENTICATING && !f->connected,
              "...and WIFI_OK means AUTHENTICATING, NOT connected");
        /* An ISR reporting "scan finished" for an interface that is not
         * scanning must be refused. Checked HERE, mid-association, because
         * from IDLE the IDLE->IDLE edge would refuse it anyway and the state
         * gate would look present even if it were gone. */
        CHECK(wifi_scan_complete(&dev, sta) == WIFI_ESTATE,
              "a scan-done ISR for a mid-association interface is ESTATE");
        CHECK(f->state == WIFI_STATE_AUTHENTICATING,
              "...and it did NOT knock the association back to IDLE");
        CHECK(vr.ch_calls == 1 && vr.last_channel == 6 && vr.last_freq == 2437,
              "the radio was tuned to channel 6 / 2437 MHz first");
        CHECK(vr.tx_count == 1, "exactly one frame went out");
        CHECK(vr.tx_len[0] == 30, "an Open System Authentication frame is 24 + 6 = 30 bytes");
        CHECK(vr.tx[0][0] == 0xB0, "frame control byte 0 = 0xB0: type 0 (mgmt), subtype 11 (auth)");
        CHECK(memcmp(vr.tx[0] + 4, bssid, 6) == 0, "addr1 is the AP");
        CHECK(memcmp(vr.tx[0] + 10, our_mac, 6) == 0, "addr2 is us");
        CHECK(memcmp(vr.tx[0] + 16, bssid, 6) == 0, "addr3 is the BSSID");
        CHECK(vr.tx[0][24] == 0 && vr.tx[0][25] == 0, "auth algorithm 0 = Open System");
        CHECK(vr.tx[0][26] == 1 && vr.tx[0][27] == 0, "auth transaction sequence 1");
        CHECK(vr.tx[0][28] == 0 && vr.tx[0][29] == 0, "status code 0");
        CHECK(f->sup.pmk_valid, "the PMK was derived at connect time");
        {
            uint8_t want[32];
            (void) wifi_wpa_pmk("correcthorse", "ZXV-NET", want);
            CHECK(memcmp(f->sup.pmk, want, 32) == 0,
                  "...and it is exactly PBKDF2(passphrase, SSID, 4096, 32)");
        }
        CHECK(f->tx_packets == 1 && f->tx_bytes == 30,
              "the counters record exactly the 30 bytes that left");

        /* a deauth from somebody ELSE must not touch us */
        {
            uint8_t evil[64], other[6] = {0xDE, 0xAD, 0, 0, 0, 1};
            uint32_t en = mgmt_hdr(evil, WIFI_STYPE_DEAUTH, our_mac, other, other, 1);
            put16le(evil + en, 3);
            en += 2;
            CHECK(wifi_rx_mgmt(&dev, sta, evil, en, -55) == WIFI_EAGAIN,
                  "a deauth from a DIFFERENT BSSID is ignored");
            CHECK(f->state == WIFI_STATE_AUTHENTICATING, "...and our state is untouched");
        }

        /* auth response */
        {
            uint8_t resp[64];
            uint32_t rn = mgmt_hdr(resp, WIFI_STYPE_AUTH, our_mac, bssid, bssid, 1);
            put16le(resp + rn, 0);     /* algorithm  */
            put16le(resp + rn + 2, 2); /* sequence 2 */
            put16le(resp + rn + 4, 0); /* status ok  */
            rn += 6;
            CHECK(wifi_rx_mgmt(&dev, sta, resp, rn, -55) == WIFI_STYPE_AUTH,
                  "the authentication response is accepted");
            CHECK(f->state == WIFI_STATE_ASSOCIATING, "state advances to ASSOCIATING");
            CHECK(vr.tx_count == 2, "an association request went out automatically");
            CHECK(vr.tx[1][0] == 0x00,
                  "frame control byte 0 = 0x00: type 0 (mgmt), subtype 0 (assoc req)");
            CHECK(vr.tx[1][24] == 0x11 && vr.tx[1][25] == 0x00,
                  "capability info = ESS | Privacy (0x0011) for a WPA2 join");
            CHECK(vr.tx[1][28] == WIFI_EID_SSID && vr.tx[1][29] == 7 &&
                      memcmp(vr.tx[1] + 30, "ZXV-NET", 7) == 0,
                  "the SSID element carries \"ZXV-NET\" at the start of the body");
        }

        /* association response */
        {
            uint8_t resp[64];
            uint32_t rn = mgmt_hdr(resp, WIFI_STYPE_ASSOC_RESP, our_mac, bssid, bssid, 2);
            put16le(resp + rn, 0x11);       /* capability */
            put16le(resp + rn + 2, 0);      /* status ok  */
            put16le(resp + rn + 4, 0xC001); /* AID        */
            rn += 6;
            CHECK(wifi_rx_mgmt(&dev, sta, resp, rn, -55) == WIFI_STYPE_ASSOC_RESP,
                  "the association response is accepted");
            CHECK(f->state == WIFI_STATE_HANDSHAKING,
                  "a WPA2 network goes to HANDSHAKING, NOT to CONNECTED");
            CHECK(!f->connected, "...and connected is still false: no key, no link");
        }

        /* --- the 4-way handshake --- */
        {
            uint8_t m1[256], reply[256];
            uint32_t rlen = 0;
            uint8_t ap_anonce[32];
            for (int i = 0; i < 32; i++) ap_anonce[i] = (uint8_t) (0xE0 + i);

            uint32_t m1n = make_eapol(m1, (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_ACK), 100,
                                      ap_anonce, 0, 0, 0);

            /* (a) a backend with NO entropy source at all */
            {
                wifi_ops_t no_rng = ops;
                no_rng.get_random = 0;
                (void) wifi_bind_ops(&dev, &no_rng);
                CHECK(wifi_wpa_rx_eapol(&dev, sta, m1, m1n, reply, sizeof reply, &rlen) ==
                          WIFI_ENODEV,
                      "M1 with NO entropy source is ENODEV — an SNonce is never invented");
                CHECK(rlen == 0 && !f->sup.ptk_derived, "...and no key material was produced");
                (void) wifi_bind_ops(&dev, &ops);
            }
            /* (b) an entropy source that is present but fails */
            vr.have_random = 0;
            CHECK(wifi_wpa_rx_eapol(&dev, sta, m1, m1n, reply, sizeof reply, &rlen) == WIFI_EIO,
                  "an entropy source that FAILS is EIO — still no invented nonce");
            CHECK(rlen == 0 && !f->sup.ptk_derived, "...and still no key material");
            vr.have_random = 1;

            CHECK(wifi_wpa_rx_eapol(&dev, sta, m1, m1n, reply, sizeof reply, &rlen) == 1,
                  "M1 is processed once the radio can supply randomness");
            CHECK(rlen == 121, "the M2 reply is 99 + 22 bytes (it carries our RSN element)");
            CHECK(f->sup.ptk_derived && !f->sup.ptk_valid,
                  "the PTK is DERIVED but not yet installed — M3 has not been verified");

            /* rebuild the same PTK independently and check M2's MIC with it */
            uint8_t my_pmk[32], my_snonce[32];
            wifi_ptk_t my_ptk;
            (void) wifi_wpa_pmk("correcthorse", "ZXV-NET", my_pmk);
            for (int i = 0; i < 32; i++) my_snonce[i] = (uint8_t) (0x40 + i);
            CHECK(memcmp(reply + 17, my_snonce, 32) == 0,
                  "M2 carries the SNonce the radio's RNG produced, at offset 17");
            CHECK(wifi_wpa_derive_ptk(my_pmk, bssid, our_mac, ap_anonce, my_snonce, &my_ptk) ==
                      WIFI_OK,
                  "an independent PTK derivation runs");
            CHECK(memcmp(&my_ptk, &f->sup.ptk, sizeof my_ptk) == 0,
                  "...and matches the PTK the supplicant derived");
            CHECK(wifi_eapol_mic_verify(my_ptk.kck, reply, rlen, 2),
                  "M2's MIC verifies under the independently derived KCK");
            CHECK(reply[97] == 0 && reply[98] == 22 && reply[99] == WIFI_EID_RSN,
                  "M2's key data is our 22-byte RSN element");
            CHECK(reply[16] == 100, "M2 echoes the replay counter from M1");

            /* M3 with a WRONG MIC must be rejected */
            uint8_t m3[256];
            uint8_t bad_kck[16];
            memcpy(bad_kck, my_ptk.kck, 16);
            bad_kck[0] ^= 0x01;
            uint32_t m3n = make_eapol(m3,
                                      (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_MIC | WIFI_KI_ACK |
                                                  WIFI_KI_SECURE | WIFI_KI_INSTALL),
                                      101, ap_anonce, 0, 0, bad_kck);
            CHECK(wifi_wpa_rx_eapol(&dev, sta, m3, m3n, reply, sizeof reply, &rlen) == WIFI_EAUTH,
                  "an M3 signed with the WRONG key is rejected with EAUTH");
            CHECK(f->state == WIFI_STATE_HANDSHAKING && !f->connected && !f->sup.ptk_valid,
                  "...and the interface did NOT become connected");

            /* M3 with a valid MIC but a DIFFERENT ANonce (a splice attempt) */
            {
                uint8_t other_nonce[32];
                memcpy(other_nonce, ap_anonce, 32);
                other_nonce[0] ^= 0xFF;
                uint32_t bn2 = make_eapol(
                    m3,
                    (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_MIC | WIFI_KI_ACK | WIFI_KI_SECURE),
                    101, other_nonce, 0, 0, my_ptk.kck);
                CHECK(wifi_wpa_rx_eapol(&dev, sta, m3, bn2, reply, sizeof reply, &rlen) ==
                          WIFI_EAUTH,
                      "an M3 whose ANonce differs from M1's is rejected even with a good MIC");
            }

            /* a replayed counter is refused */
            {
                uint32_t bn3 = make_eapol(
                    m3,
                    (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_MIC | WIFI_KI_ACK | WIFI_KI_SECURE),
                    100, ap_anonce, 0, 0, my_ptk.kck);
                CHECK(wifi_wpa_rx_eapol(&dev, sta, m3, bn3, reply, sizeof reply, &rlen) ==
                          WIFI_EAUTH,
                      "an M3 replaying M1's counter (100) is refused");
            }

            /* the real M3 */
            m3n = make_eapol(m3,
                             (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_MIC | WIFI_KI_ACK |
                                         WIFI_KI_SECURE | WIFI_KI_INSTALL),
                             101, ap_anonce, 0, 0, my_ptk.kck);
            CHECK(wifi_wpa_rx_eapol(&dev, sta, m3, m3n, reply, sizeof reply, &rlen) == 3,
                  "a correctly signed M3 is accepted");
            CHECK(rlen == 99, "the M4 reply is 99 bytes and carries no key data");
            CHECK(reply[6] == (uint8_t) ((2 | WIFI_KI_PAIRWISE | WIFI_KI_MIC | WIFI_KI_SECURE) &
                                         0xFF) &&
                      (reply[5] & 0x03) == 0x03,
                  "M4's key info sets MIC and Secure (0x030A) and clears Ack");
            CHECK(wifi_eapol_mic_verify(my_ptk.kck, reply, rlen, 2), "M4's MIC verifies");
            CHECK(reply[16] == 101, "M4 echoes M3's replay counter");
            CHECK(f->state == WIFI_STATE_CONNECTED && f->connected,
                  "the handshake completes and the interface is CONNECTED");
            CHECK(f->sup.ptk_valid, "the pairwise TK is installed");
            CHECK(!f->sup.gtk_valid,
                  "the GROUP key is NOT installed — key-data unwrap is not implemented (L4)");
            CHECK(dev.irq_connected, "the connected IRQ flag is raised");

            /* version 3 (AES-CMAC) is refused outright */
            {
                uint8_t v3[256];
                uint32_t v3n = make_eapol(v3, (uint16_t) (3 | WIFI_KI_PAIRWISE | WIFI_KI_ACK), 200,
                                          ap_anonce, 0, 0, 0);
                wifi_interface_t *fi = wifi_get_interface(&dev, sta);
                wifi_assoc_state_t save = fi->state;
                fi->state = WIFI_STATE_HANDSHAKING;
                CHECK(wifi_wpa_rx_eapol(&dev, sta, v3, v3n, reply, sizeof reply, &rlen) ==
                          WIFI_ENOTSUP,
                      "key descriptor version 3 (AES-CMAC) is refused, not faked");
                fi->state = save;
            }
        }

        /* coverage on a healthy device */
        CHECK(wifi_verify_coverage(&dev), "coverage passes: one coherent interface, radio bound");
        CHECK(dev.coverage_r == 1.0 && dev.coverage_l == 1.0, "r = 1.0 and l = 1.0");

        /* now break it, three ways */
        f->connected = true;
        f->state = WIFI_STATE_IDLE; /* claims a link while idle */
        CHECK(!wifi_verify_coverage(&dev),
              "coverage FAILS when an interface claims connected while IDLE");
        CHECK(dev.coverage_r == 0.0, "...and r drops to 0.0");
        f->state = WIFI_STATE_CONNECTED;
        memset(f->bssid, 0, 6);
        CHECK(!wifi_verify_coverage(&dev),
              "coverage FAILS when a CONNECTED interface holds no BSSID");
        memcpy(f->bssid, bssid, 6);
        CHECK(wifi_verify_coverage(&dev), "restoring the BSSID restores coverage");
        /* Every OTHER thing a CONNECTED interface must really hold, broken one
         * at a time and restored, so each sub-check is shown to carry weight
         * on its own rather than being covered by its neighbours. */
        {
            char save_ssid[WIFI_MAX_SSID_LEN];
            memcpy(save_ssid, f->ssid, sizeof save_ssid);
            f->ssid[0] = '\0';
            CHECK(!wifi_verify_coverage(&dev),
                  "coverage FAILS when a CONNECTED interface holds no SSID");
            memcpy(f->ssid, save_ssid, sizeof save_ssid);
            CHECK(wifi_verify_coverage(&dev), "...and passes again when the SSID is back");

            uint32_t save_ch = f->channel;
            f->channel = 15; /* not a channel in any band */
            CHECK(!wifi_verify_coverage(&dev),
                  "coverage FAILS when a CONNECTED interface sits on channel 15, which "
                  "does not exist");
            f->channel = save_ch;
            CHECK(wifi_verify_coverage(&dev), "...and passes again on a real channel");

            f->up = false;
            CHECK(!wifi_verify_coverage(&dev),
                  "coverage FAILS when a CONNECTED interface is not even up");
            f->up = true;
            CHECK(wifi_verify_coverage(&dev), "...and passes again once it is up");

            uint8_t save_ps = f->power_save_level;
            f->power_save_level = 7; /* only 0..2 exist */
            CHECK(!wifi_verify_coverage(&dev),
                  "coverage FAILS on an out-of-range power-save level");
            f->power_save_level = save_ps;
            CHECK(wifi_verify_coverage(&dev), "...and passes again at a real level");
        }
        {
            /* r is a FRACTION, not a boolean: with a second, incoherent
             * interface present it must land at exactly one half. */
            uint32_t extra = wifi_create_interface(&dev, "wlan1", WIFI_MODE_STATION);
            wifi_interface_t *xf = wifi_get_interface(&dev, extra);
            CHECK(extra == 2 && wifi_verify_coverage(&dev) && dev.coverage_r == 1.0,
                  "a second, idle-and-honest interface keeps r at 1.0");
            xf->connected = true; /* claims a link it does not hold */
            CHECK(!wifi_verify_coverage(&dev) && dev.coverage_r == 0.5,
                  "one honest interface out of two puts r at exactly 0.5, and 0.5 fails");
            xf->connected = false;
            CHECK(wifi_verify_coverage(&dev) && dev.coverage_r == 1.0,
                  "and r returns to 1.0 when it stops claiming");
            dev.num_ifaces = 1; /* drop it again for what follows */
            memset(xf, 0, sizeof *xf);
        }
        wifi_unbind_ops(&dev);
        CHECK(!wifi_verify_coverage(&dev) && dev.coverage_l == 0.0,
              "coverage FAILS with the radio unbound — a device with no radio covers nothing");
        CHECK(wifi_bind_ops(&dev, &ops) == WIFI_OK && wifi_verify_coverage(&dev),
              "and passes again once the radio is back");

        /* disconnect */
        CHECK(wifi_disconnect(&dev, sta) == WIFI_OK, "disconnect succeeds");
        CHECK(f->state == WIFI_STATE_IDLE && !f->connected, "the interface is IDLE again");
        CHECK(f->ssid[0] == '\0' && wifi_bssid_is_zero(f->bssid), "the association is cleared");
        {
            uint8_t zero32[32];
            memset(zero32, 0, 32);
            CHECK(memcmp(f->sup.pmk, zero32, 32) == 0 && !f->sup.pmk_valid && !f->sup.ptk_valid,
                  "the PMK and PTK are wiped — key material does not outlive the association");
        }
        CHECK(wifi_disconnect(&dev, sta) == WIFI_ESTATE, "disconnecting twice is ESTATE");
    }

    /* ================================================================= */
    printf("\n--- association failure paths ---\n");
    /* ================================================================= */
    {
        wifi_device_t fd;
        static vradio_t fv;
        memset(&fv, 0, sizeof fv);
        wifi_init(&fd, "failpath");
        uint32_t fs = wifi_create_interface(&fd, "wlan0", WIFI_MODE_STATION);
        wifi_ops_t fops = vradio_ops_for(&fv);
        (void) wifi_bind_ops(&fd, &fops);
        wifi_interface_t *fi = wifi_get_interface(&fd, fs);
        uint8_t bssid[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        uint8_t bcn[512];
        uint32_t bn = make_beacon(bcn, bssid, "OpenNet", 1, SEC_OPEN, PHY_G);
        wifi_scan_result_t r;
        (void) wifi_parse_beacon(bcn, bn, -55, 2412, &r);
        (void) wifi_scan_add_result(&fd, &r);

        CHECK(wifi_connect(&fd, fs, "OpenNet", 0, WIFI_SEC_OPEN) == WIFI_OK,
              "an OPEN network connects with no password");
        CHECK(fi->state == WIFI_STATE_AUTHENTICATING, "state is AUTHENTICATING");

        uint8_t resp[64];
        uint32_t rn = mgmt_hdr(resp, WIFI_STYPE_AUTH, fi->mac, bssid, bssid, 1);
        put16le(resp + rn, 0);
        put16le(resp + rn + 2, 2);
        put16le(resp + rn + 4, 13);
        rn += 6;
        CHECK(wifi_rx_mgmt(&fd, fs, resp, rn, -55) == WIFI_EAUTH,
              "an authentication response with status 13 is a failure");
        CHECK(fi->state == WIFI_STATE_FAILED && !fi->connected,
              "...and the interface goes to FAILED, never to CONNECTED");
        CHECK(fd.irq_auth_failed, "the auth-failed IRQ flag is raised");
        CHECK(wifi_state_set(&fd, fs, WIFI_STATE_IDLE) == WIFI_OK, "FAILED can be reset to IDLE");

        /* now a successful open-system join, and an assoc failure */
        fd.irq_auth_failed = false;
        CHECK(wifi_connect(&fd, fs, "OpenNet", 0, WIFI_SEC_OPEN) == WIFI_OK, "retry connects");
        rn = mgmt_hdr(resp, WIFI_STYPE_AUTH, fi->mac, bssid, bssid, 1);
        put16le(resp + rn, 0);
        put16le(resp + rn + 2, 2);
        put16le(resp + rn + 4, 0);
        rn += 6;
        (void) wifi_rx_mgmt(&fd, fs, resp, rn, -55);
        rn = mgmt_hdr(resp, WIFI_STYPE_ASSOC_RESP, fi->mac, bssid, bssid, 2);
        put16le(resp + rn, 0x01);
        put16le(resp + rn + 2, 17);
        put16le(resp + rn + 4, 0);
        rn += 6;
        CHECK(wifi_rx_mgmt(&fd, fs, resp, rn, -55) == WIFI_EAUTH,
              "association status 17 (AP full) is a failure");
        CHECK(fi->state == WIFI_STATE_FAILED, "...and the interface goes to FAILED");

        /* a clean OPEN join reaches CONNECTED without any handshake */
        (void) wifi_state_set(&fd, fs, WIFI_STATE_IDLE);
        fd.irq_auth_failed = false;
        (void) wifi_connect(&fd, fs, "OpenNet", 0, WIFI_SEC_OPEN);
        rn = mgmt_hdr(resp, WIFI_STYPE_AUTH, fi->mac, bssid, bssid, 1);
        put16le(resp + rn, 0);
        put16le(resp + rn + 2, 2);
        put16le(resp + rn + 4, 0);
        rn += 6;
        (void) wifi_rx_mgmt(&fd, fs, resp, rn, -55);
        rn = mgmt_hdr(resp, WIFI_STYPE_ASSOC_RESP, fi->mac, bssid, bssid, 2);
        put16le(resp + rn, 0x01);
        put16le(resp + rn + 2, 0);
        put16le(resp + rn + 4, 0xC001);
        rn += 6;
        CHECK(wifi_rx_mgmt(&fd, fs, resp, rn, -55) == WIFI_STYPE_ASSOC_RESP &&
                  fi->state == WIFI_STATE_CONNECTED && fi->connected,
              "an OPEN network reaches CONNECTED straight from ASSOCIATING");

        /* a deauth from the real BSSID does take us down */
        rn = mgmt_hdr(resp, WIFI_STYPE_DEAUTH, fi->mac, bssid, bssid, 3);
        put16le(resp + rn, 3);
        rn += 2;
        CHECK(wifi_rx_mgmt(&fd, fs, resp, rn, -55) == WIFI_STYPE_DEAUTH &&
                  fi->state == WIFI_STATE_IDLE && !fi->connected,
              "a deauth from our own BSSID does disconnect us");
        CHECK(fd.irq_disconnected, "the disconnected IRQ flag is raised");
    }

    /* ================================================================= */
    printf("\n--- AP mode ---\n");
    /* ================================================================= */
    {
        wifi_device_t ad;
        static vradio_t av;
        memset(&av, 0, sizeof av);
        wifi_init(&ad, "ap-dev");
        uint32_t ap = wifi_create_interface(&ad, "ap0", WIFI_MODE_AP);
        wifi_ops_t aops = vradio_ops_for(&av);
        wifi_interface_t *ai = wifi_get_interface(&ad, ap);

        CHECK(wifi_start_ap(&ad, ap, "MyAP", "hunter22", 6) == WIFI_ENODEV,
              "starting an AP with no radio is ENODEV");
        CHECK(ai->state == WIFI_STATE_IDLE && !ai->connected,
              "...and the interface is not beaconing");
        (void) wifi_bind_ops(&ad, &aops);
        CHECK(wifi_start_ap(&ad, ap, "MyAP", "short", 6) == WIFI_EINVAL,
              "a 5-character AP passphrase is refused");
        CHECK(wifi_start_ap(&ad, ap, "MyAP", "hunter22", 15) == WIFI_EINVAL,
              "channel 15 is refused");
        CHECK(wifi_start_ap(&ad, ap, "", "hunter22", 6) == WIFI_EINVAL,
              "an empty AP SSID is refused");
        CHECK(wifi_start_ap(&ad, ap, "MyAP", "hunter22", 6) == WIFI_OK && av.ap_start_calls == 1,
              "the AP starts and the backend was called once");
        CHECK(ai->state == WIFI_STATE_BEACONING && ai->connected && ai->up,
              "the interface is BEACONING");
        CHECK(memcmp(ai->bssid, ai->mac, 6) == 0, "an AP's BSSID is its own MAC address");
        CHECK(wifi_start_ap(&ad, ap, "MyAP", "hunter22", 6) == WIFI_ESTATE,
              "starting it twice is ESTATE");

        /* the beacon we emit must parse back to what we configured */
        uint8_t bcn[512];
        int bl = wifi_build_beacon(&ad, ap, bcn, sizeof bcn);
        printf("       our beacon is %d bytes\n", bl);
        CHECK(bl > 40, "a beacon is built");
        CHECK(bcn[0] == 0x80, "frame control byte 0 = 0x80: type 0 (mgmt), subtype 8 (beacon)");
        CHECK(memcmp(bcn + 4, "\xff\xff\xff\xff\xff\xff", 6) == 0, "addr1 is broadcast");
        CHECK(bcn[24] == 0 && bcn[31] == 0, "the TSF timestamp is left zero (no radio clock)");
        CHECK(bcn[32] == 100 && bcn[33] == 0, "beacon interval 100 TU, little endian");
        CHECK(bcn[34] == 0x11 && bcn[35] == 0x00, "capability = ESS | Privacy for a secured AP");
        {
            wifi_scan_result_t br;
            CHECK(wifi_parse_beacon(bcn, (uint32_t) bl, -30, 2437, &br) == WIFI_OK,
                  "our own beacon parses");
            CHECK(strcmp(br.ssid, "MyAP") == 0 && br.channel == 6,
                  "...back to SSID \"MyAP\" on channel 6");
            CHECK(br.security == WIFI_SEC_WPA2, "...advertising WPA2-PSK");
            CHECK(br.max_rate_mbps == 54, "...with a 54 Mbps top legacy rate");
            CHECK(memcmp(br.bssid, ai->mac, 6) == 0, "...and our own BSSID");
        }
        CHECK(wifi_build_beacon(&ad, ap, bcn, 20) == WIFI_EMSGSIZE,
              "building a beacon into a 20-byte buffer is refused");
        CHECK(wifi_stop_ap(&ad, ap) == WIFI_OK && av.ap_stop_calls == 1, "the AP stops");
        CHECK(ai->state == WIFI_STATE_IDLE && !ai->connected && !ai->up,
              "the interface is idle again");
        CHECK(wifi_stop_ap(&ad, ap) == WIFI_ESTATE, "stopping it twice is ESTATE");
        CHECK(wifi_build_beacon(&ad, ap, bcn, sizeof bcn) == WIFI_EINVAL,
              "and a stopped AP has no SSID, so there is nothing to beacon");
    }

    /* ================================================================= */
    printf("\n--- backend failures, tx/rx counters and IRQs ---\n");
    /* ================================================================= */
    {
        wifi_device_t td;
        static vradio_t tv;
        memset(&tv, 0, sizeof tv);
        wifi_init(&td, "txdev");
        uint32_t ts = wifi_create_interface(&td, "wlan0", WIFI_MODE_STATION);
        wifi_ops_t tops = vradio_ops_for(&tv);
        (void) wifi_bind_ops(&td, &tops);
        wifi_interface_t *ti = wifi_get_interface(&td, ts);

        uint8_t payload[100];
        memset(payload, 0xC3, sizeof payload);
        CHECK(wifi_tx_packet(&td, ts, payload, 100) == 100, "tx_packet reports 100 bytes sent");
        CHECK(tv.tx_count == 1 && tv.tx_len[0] == 100 && tv.tx[0][0] == 0xC3,
              "...and the radio really received those 100 bytes");
        CHECK(ti->tx_packets == 1 && ti->tx_bytes == 100, "counters read 1 packet / 100 bytes");

        tv.tx_fail = 1;
        CHECK(wifi_tx_packet(&td, ts, payload, 100) == WIFI_EIO,
              "a backend transmit failure surfaces as EIO");
        CHECK(ti->tx_packets == 1 && ti->tx_bytes == 100,
              "...and the counters did NOT move for the frame that failed");
        CHECK(ti->tx_dropped == 1, "...it was counted as a drop instead");
        tv.tx_fail = 0;

        CHECK(wifi_tx_enqueue(&td, payload, 50) == WIFI_OK &&
                  wifi_tx_enqueue(&td, payload, 60) == WIFI_OK,
              "two frames queue up");
        CHECK(ti->tx_packets == 1, "...without counting as transmits");
        CHECK(wifi_tx_flush(&td, ts) == 2, "flush reports 2 frames sent");
        CHECK(tv.tx_count == 3 && tv.tx_len[1] == 50 && tv.tx_len[2] == 60,
              "...and the radio got them in order, 50 then 60 bytes");
        CHECK(ti->tx_packets == 3 && ti->tx_bytes == 210, "counters now read 3 / 210 bytes");
        CHECK(wifi_tx_pending(&td) == 0 && wifi_tx_flush(&td, ts) == 0,
              "an empty queue flushes zero frames");
        {
            /* A flush that the backend refuses must not count the refused
             * frame as sent. Two shapes: the failure hits the FIRST frame
             * (nothing was sent, so EIO), and it hits a LATER one (some really
             * did go, so the count of those is returned). */
            uint64_t p0 = ti->tx_packets, b0 = ti->tx_bytes, d0 = ti->tx_dropped;
            CHECK(wifi_tx_enqueue(&td, payload, 70) == WIFI_OK, "one frame queues");
            tv.tx_fail = 1;
            CHECK(wifi_tx_flush(&td, ts) == WIFI_EIO,
                  "a flush whose very first frame is refused reports EIO");
            CHECK(ti->tx_packets == p0 && ti->tx_bytes == b0,
                  "...and NOTHING was counted as transmitted");
            CHECK(ti->tx_dropped == d0 + 1, "...the frame was counted as a drop instead");
            tv.tx_fail = 0;
            /* the refused frame was consumed from the ring; queue two more */
            uint32_t txc0 = tv.tx_count;
            CHECK(wifi_tx_enqueue(&td, payload, 71) == WIFI_OK &&
                      wifi_tx_enqueue(&td, payload, 72) == WIFI_OK,
                  "two more queue up");
            tv.tx_count = VR_TX_SLOTS - 1; /* the radio has room for one */
            p0 = ti->tx_packets;
            b0 = ti->tx_bytes;
            d0 = ti->tx_dropped;
            CHECK(wifi_tx_flush(&td, ts) == 1,
                  "a flush that sends one frame and is then refused reports 1, not 2");
            CHECK(ti->tx_packets == p0 + 1 && ti->tx_bytes == b0 + 71,
                  "...and counted exactly the 71 bytes that really left");
            CHECK(ti->tx_dropped == d0 + 1, "...with the refused frame counted as a drop");
            tv.tx_count = txc0; /* restore the log for later checks */
        }

        /* rx straight from the backend */
        memset(tv.rx[0], 0x5E, 40);
        tv.rx_len[0] = 40;
        tv.rx_head = 1;
        uint8_t got[100];
        CHECK(wifi_rx_packet(&td, ts, got, sizeof got) == 40 && got[0] == 0x5E,
              "rx_packet pulls a 40-byte frame straight from the radio");
        CHECK(ti->rx_packets == 1 && ti->rx_bytes == 40, "rx counters read 1 / 40");
        CHECK(wifi_rx_packet(&td, ts, got, sizeof got) == WIFI_EAGAIN,
              "an empty radio reports EAGAIN, not a stale frame");

        /* the IRQ drain path */
        for (int i = 0; i < 3; i++) {
            memset(tv.rx[i], (uint8_t) (0x10 + i), 64);
            tv.rx_len[i] = 64;
        }
        tv.rx_tail = 0;
        tv.rx_head = 3;
        td.irq_rx_ready = true;
        wifi_handle_irq(&td);
        CHECK(wifi_rx_pending(&td) == 3 * 66, "the IRQ drained 3 frames into the rx ring");
        CHECK(ti->rx_packets == 4 && ti->rx_bytes == 40 + 3 * 64,
              "...and counted exactly 3 more arrivals, 64 bytes each");
        CHECK(wifi_rx_packet(&td, ts, got, sizeof got) == 64 && got[0] == 0x10,
              "the first drained frame comes back first");

        /* A frame the ISR pulls off the radio and then cannot place must be
         * ACCOUNTED FOR. Two ways that happens: the backend itself fails, and
         * the ring is full. Neither may vanish silently. */
        {
            uint64_t drops0 = ti->rx_dropped, pkts0 = ti->rx_packets;
            tv.rx_fail = 1;
            td.irq_rx_ready = true;
            wifi_handle_irq(&td);
            CHECK(ti->rx_dropped == drops0 + 1,
                  "an rx_poll failure during the IRQ drain is counted as a drop");
            CHECK(ti->rx_packets == pkts0, "...and is NOT counted as an arrival — nothing arrived");
            tv.rx_fail = 0;
        }
        {
            /* Fill the rx ring right up, so that the 64-byte frame the radio
             * is about to hand the ISR genuinely does not fit. */
            uint8_t chunk[1000];
            int guard;
            memset(chunk, 0x77, sizeof chunk);
            /* Bounded: a 4096-byte ring cannot swallow more than a handful of
             * these, so an unbounded loop here would hang rather than fail if
             * the space check ever went missing. */
            for (guard = 0; guard < 16; guard++)
                if (wifi_rx_inject(&td, ts, chunk, 1000) != WIFI_OK) break;
            CHECK(guard < 16, "the 4096-byte ring stops accepting 1000-byte frames");
            for (guard = 0; guard < 128; guard++)
                if (wifi_rx_inject(&td, ts, chunk, 64) != WIFI_OK) break;
            CHECK(guard < 128, "...and then stops accepting 64-byte frames too");
            CHECK(wifi_rx_inject(&td, ts, chunk, 64) == WIFI_ENOSPC,
                  "the rx ring is now too full for another 64-byte frame");
            uint64_t drops0 = ti->rx_dropped, pkts0 = ti->rx_packets;
            uint32_t pend0 = wifi_rx_pending(&td);
            for (int i = 0; i < 2; i++) {
                memset(tv.rx[i], 0x99, 64);
                tv.rx_len[i] = 64;
            }
            tv.rx_tail = 0;
            tv.rx_head = 2;
            td.irq_rx_ready = true;
            wifi_handle_irq(&td);
            CHECK(ti->rx_dropped == drops0 + 1,
                  "a frame the ISR pulled but could not fit is counted as a drop");
            CHECK(ti->rx_packets == pkts0 && wifi_rx_pending(&td) == pend0,
                  "...and neither the arrival count nor the ring moved for it");
            /* drain the ring again for the checks that follow */
            uint8_t sink[1200];
            for (int g = 0; g < 256 && wifi_rx_packet(&td, ts, sink, sizeof sink) > 0; g++) {
            }
            tv.rx_tail = tv.rx_head;
        }
        {
            /* An ISR on a device with NO interfaces must not pull frames off
             * the radio at all: there is nowhere to put them and nowhere to
             * count them, so consuming one would lose it without trace. */
            wifi_device_t nd;
            static vradio_t nv;
            memset(&nv, 0, sizeof nv);
            wifi_init(&nd, "no-ifaces");
            wifi_ops_t nops = vradio_ops_for(&nv);
            (void) wifi_bind_ops(&nd, &nops);
            memset(nv.rx[0], 0xAB, 64);
            nv.rx_len[0] = 64;
            nv.rx_tail = 0;
            nv.rx_head = 1;
            nd.irq_rx_ready = true;
            wifi_handle_irq(&nd);
            CHECK(nv.rx_tail == 0,
                  "an IRQ on a device with no interfaces leaves the frame ON the radio "
                  "rather than consuming and discarding it");
            CHECK(wifi_rx_pending(&nd) == 0, "...and the rx ring is still empty");
        }

        td.irq_auth_failed = true;
        (void) wifi_state_set(&td, ts, WIFI_STATE_AUTHENTICATING);
        wifi_handle_irq(&td);
        CHECK(ti->state == WIFI_STATE_FAILED && !td.irq_auth_failed,
              "an auth-failed IRQ moves a mid-association interface to FAILED and clears itself");

        CHECK(wifi_set_power_save(&td, ts, 2) == WIFI_OK && tv.ps_calls == 1 && tv.last_ps == 2 &&
                  ti->power_save_level == 2,
              "power save level 2 reaches the radio and is recorded");
        CHECK(wifi_set_channel(&td, ts, 11) == WIFI_OK && tv.last_freq == 2462 && ti->channel == 11,
              "set_channel 11 tunes the radio to 2462 MHz");
        CHECK(wifi_set_freq(&td, ts, 5745) == WIFI_OK && ti->channel == 149 &&
                  ti->band == WIFI_BAND_5GHZ,
              "set_freq 5745 MHz selects 5 GHz channel 149");
        CHECK(wifi_set_freq(&td, ts, 5747) == WIFI_EINVAL, "5747 MHz is not a channel");

        /* TX power: the ops member exists, so it must actually be driven. */
        CHECK(td.reg_tx_power == 0, "TX power starts unset");
        CHECK(wifi_set_tx_power(&td, ts, 20) == WIFI_OK && tv.txp_calls == 1 && tv.last_dbm == 20 &&
                  td.reg_tx_power == 20,
              "set_tx_power 20 dBm reaches the radio and is recorded");
        CHECK(wifi_set_tx_power(&td, ts, 31) == WIFI_EINVAL,
              "31 dBm is above the 30 dBm ceiling and is refused");
        CHECK(wifi_set_tx_power(&td, ts, 255) == WIFI_EINVAL,
              "255 dBm is not a transmit power, it is a typo");
        CHECK(tv.txp_calls == 1 && td.reg_tx_power == 20,
              "...and neither refusal reached the radio or moved the register");
        td.max_tx_power = 23; /* a driver reporting what the radio can do */
        CHECK(wifi_set_tx_power(&td, ts, 24) == WIFI_EINVAL,
              "asking for 24 dBm from a radio that tops out at 23 is refused");
        CHECK(wifi_set_tx_power(&td, ts, 23) == WIFI_OK && td.reg_tx_power == 23,
              "...while 23 dBm, exactly its maximum, is granted");
        td.max_tx_power = 0;
        tv.txp_fail = 1;
        CHECK(wifi_set_tx_power(&td, ts, 10) == WIFI_EIO,
              "a backend that refuses the power change surfaces as EIO");
        CHECK(td.reg_tx_power == 23,
              "...and the register still reads 23 — it records what the radio DID, not "
              "what we asked for");
        tv.txp_fail = 0;
        CHECK(wifi_set_tx_power(&td, 99, 10) == WIFI_EINVAL,
              "an interface that does not exist is EINVAL");
        {
            wifi_device_t nd2;
            wifi_init(&nd2, "no-radio-txp");
            uint32_t n2 = wifi_create_interface(&nd2, "w", WIFI_MODE_STATION);
            CHECK(wifi_set_tx_power(&nd2, n2, 15) == WIFI_ENODEV,
                  "with no radio bound, set_tx_power is ENODEV");
            CHECK(nd2.reg_tx_power == 0, "...and the register did not move");
            CHECK(wifi_set_tx_power(&nd2, n2, 99) == WIFI_EINVAL,
                  "...while a bad power is still EINVAL without a radio (args first)");
        }
        { /* an ops struct with no set_tx_power is not a power-capable radio */
            wifi_device_t nd3;
            static vradio_t nv3;
            memset(&nv3, 0, sizeof nv3);
            wifi_init(&nd3, "no-txp-op");
            uint32_t n3 = wifi_create_interface(&nd3, "w", WIFI_MODE_STATION);
            wifi_ops_t partial = vradio_ops_for(&nv3);
            partial.set_tx_power = 0;
            (void) wifi_bind_ops(&nd3, &partial);
            CHECK(wifi_set_tx_power(&nd3, n3, 15) == WIFI_ENODEV,
                  "a bound radio with no set_tx_power entry point is ENODEV, not a "
                  "silent success");
            CHECK(nd3.reg_tx_power == 0, "...and its register did not move either");
        }

        /* MAC adoption from the backend */
        tv.have_mac = 1;
        tv.mac_base = 0x77;
        CHECK(wifi_bind_ops(&td, &tops) == WIFI_OK && !ti->mac_is_placeholder &&
                  ti->mac[0] == 0x00 && ti->mac[4] == 0x77,
              "rebinding a backend that knows the real MAC replaces the placeholder");
        {
            uint8_t mc[6] = {0x01, 0, 0, 0, 0, 1};
            CHECK(wifi_set_mac(&td, ts, mc) == WIFI_EINVAL,
                  "a multicast address is refused as a station MAC");
        }
    }

    /* ================================================================= */
    printf("\n--- wifi_wpa_start: arming the supplicant by hand ---\n");
    /* ================================================================= */
    {
        wifi_device_t wd;
        static vradio_t wv;
        memset(&wv, 0, sizeof wv);
        wifi_init(&wd, "wpastart");
        uint32_t ws = wifi_create_interface(&wd, "wlan0", WIFI_MODE_STATION);
        wifi_ops_t wops = vradio_ops_for(&wv);
        (void) wifi_bind_ops(&wd, &wops);
        wifi_interface_t *wi = wifi_get_interface(&wd, ws);

        CHECK(wifi_wpa_start(&wd, ws, "correcthorse") == WIFI_ESTATE,
              "arming the supplicant with no SSID set is ESTATE");
        CHECK(!wi->sup.pmk_valid, "...and no PMK was produced");

        wifi_ssid_set(wi->ssid, "ZXV-NET");
        CHECK(wifi_wpa_start(&wd, ws, "correcthorse") == WIFI_ESTATE,
              "arming it while IDLE is ESTATE — the 4-way handshake belongs to an "
              "association in progress");
        CHECK(!wi->sup.pmk_valid, "...and still no PMK");

        wi->state = WIFI_STATE_ASSOCIATING;
        CHECK(wifi_wpa_start(&wd, ws, "short") == WIFI_EINVAL,
              "a 5-character passphrase is refused even in the right state");
        CHECK(!wi->sup.pmk_valid, "...and produced no PMK");
        CHECK(wifi_wpa_start(&wd, ws, 0) == WIFI_EINVAL, "a NULL passphrase is refused");

        CHECK(wifi_wpa_start(&wd, ws, "correcthorse") == WIFI_OK,
              "arming it from ASSOCIATING with a legal passphrase succeeds");
        CHECK(wi->sup.pmk_valid, "...and the PMK is now valid");
        {
            uint8_t want[32];
            (void) wifi_wpa_pmk("correcthorse", "ZXV-NET", want);
            CHECK(memcmp(wi->sup.pmk, want, 32) == 0,
                  "...and equals PBKDF2(passphrase, SSID, 4096, 32) exactly — the same "
                  "value wifi_connect() would have derived");
            uint8_t other[32];
            (void) wifi_wpa_pmk("correcthorse", "OtherNet", other);
            CHECK(memcmp(wi->sup.pmk, other, 32) != 0,
                  "...and the SSID really is the salt: a different SSID gives a different PMK");
        }
        CHECK(memcmp(wi->sup.aa, wi->bssid, 6) == 0 && memcmp(wi->sup.spa, wi->mac, 6) == 0,
              "the authenticator and supplicant addresses are taken from the interface");
        CHECK(!wi->sup.ptk_derived && !wi->sup.ptk_valid && !wi->sup.snonce_valid,
              "a PMK is not a PTK: nothing is derived or installed until M1 arrives");

        /* the explicit-SNonce seam, so a handshake can run with no RNG */
        {
            uint8_t fixed[32];
            for (int i = 0; i < 32; i++) fixed[i] = (uint8_t) (0x90 + i);
            CHECK(wifi_wpa_set_snonce(&wd, ws, fixed) == WIFI_OK && wi->sup.snonce_valid,
                  "wifi_wpa_set_snonce fixes the SNonce explicitly");
            CHECK(memcmp(wi->sup.snonce, fixed, 32) == 0, "...to exactly the bytes given");
            CHECK(wifi_wpa_set_snonce(&wd, ws, 0) == WIFI_EINVAL, "a NULL SNonce is refused");
            CHECK(wifi_wpa_set_snonce(&wd, 99, fixed) == WIFI_EINVAL,
                  "...as is one for an interface that does not exist");
        }
        /* and with that, M1 needs no entropy source at all */
        {
            wifi_ops_t no_rng = wops;
            no_rng.get_random = 0;
            (void) wifi_bind_ops(&wd, &no_rng);
            wi->state = WIFI_STATE_HANDSHAKING;
            uint8_t m1[160], reply[160];
            uint32_t rl = 0;
            uint8_t an[32];
            for (int i = 0; i < 32; i++) an[i] = (uint8_t) (0x20 + i);
            uint32_t m1n =
                make_eapol(m1, (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_ACK), 5, an, 0, 0, 0);
            CHECK(wifi_wpa_rx_eapol(&wd, ws, m1, m1n, reply, sizeof reply, &rl) == 1,
                  "M1 is answered with NO entropy source, because the SNonce was supplied");
            CHECK(rl == 121 && memcmp(reply + 17, wi->sup.snonce, 32) == 0,
                  "...and M2 carries exactly that SNonce");
            uint8_t ptk_want[48];
            wifi_ptk_t pw;
            (void) wifi_wpa_derive_ptk(wi->sup.pmk, wi->sup.aa, wi->sup.spa, an, wi->sup.snonce,
                                       &pw);
            (void) ptk_want;
            CHECK(memcmp(&pw, &wi->sup.ptk, sizeof pw) == 0,
                  "...and the PTK matches an independent derivation from the same inputs");
        }
    }

    /* ================================================================= */
    printf("\n--- hostile input: every parser, malformed and truncated ---\n");
    /* ================================================================= */
    {
        /* Every buffer here is heap-allocated at EXACTLY the length passed in,
         * so a one-byte overread is an ASan abort rather than a quiet read
         * into stack slack. This section proves nothing about correctness; it
         * proves the parsers cannot be walked off the end of their input.
         * Build with -fsanitize=address,undefined for it to mean anything. */
        wifi_device_t hd;
        static vradio_t hv;
        memset(&hv, 0, sizeof hv);
        wifi_init(&hd, "hostile");
        uint32_t hs = wifi_create_interface(&hd, "wlan0", WIFI_MODE_STATION);
        wifi_ops_t hops = vradio_ops_for(&hv);
        (void) wifi_bind_ops(&hd, &hops);

        rng_state = 0x5EEDu;
        unsigned long parsed = 0;
        unsigned long bcn_ok = 0, bcn_refused = 0;
        unsigned long eap_ok = 0, eap_refused = 0;
        int broke_invariant = 0;

        for (int round = 0; round < 60000; round++) {
            uint32_t len = rnd() % 260;
            uint8_t *b = (uint8_t *) malloc(len ? len : 1);
            for (uint32_t i = 0; i < len; i++) b[i] = (uint8_t) rnd();
            if (len >= 24 && (round & 1)) { /* plausible mgmt shape */
                b[0] = (uint8_t) ((rnd() % 16) << 4);
                b[1] = (uint8_t) (rnd() & 0x7F);
            }
            wifi_mac_hdr_t hh;
            wifi_scan_result_t rr;
            wifi_eapol_key_t kk;
            const uint8_t *vv = 0;
            uint8_t reply[160];
            uint32_t rl = 0;

            (void) wifi_mac_hdr_parse(b, len, &hh);
            (void) wifi_parse_beacon(b, len, -50, (round & 2) ? 2437u : 0u, &rr);
            (void) wifi_find_ie(b, len, (uint8_t) (rnd() & 0xFF), &vv);
            (void) wifi_eapol_key_parse(b, len, &kk);
            (void) wifi_rx_mgmt(&hd, hs, b, len, -60);
            (void) wifi_wpa_rx_eapol(&hd, hs, b, len, reply, sizeof reply, &rl);
            if (len >= 99) {
                uint8_t kck[16], mic[16];
                memset(kck, 0x11, 16);
                (void) wifi_eapol_mic(kck, b, len, 2, mic);
                (void) wifi_eapol_mic_verify(kck, b, len, 2);
            }
            free(b);
            parsed++;
        }

        /* Exhaustive: EVERY truncation and EVERY single-bit flip of one
         * well-formed beacon. A parser that trusts an element length only
         * shows it when the length is the byte that was corrupted. */
        {
            uint8_t bssid[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
            uint8_t good[512];
            uint32_t gn = make_beacon(good, bssid, "ZXV-NET", 6, SEC_WPA2, PHY_AX);
            for (uint32_t t = 0; t <= gn; t++) {
                uint8_t *c = (uint8_t *) malloc(t ? t : 1);
                memcpy(c, good, t);
                wifi_scan_result_t rr;
                const uint8_t *vv = 0;
                (void) wifi_parse_beacon(c, t, -40, 2437, &rr);
                (void) wifi_find_ie(c, t, WIFI_EID_SSID, &vv);
                free(c);
                parsed++;
            }
            for (uint32_t i = 0; i < gn; i++)
                for (int bit = 0; bit < 8; bit++) {
                    uint8_t *c = (uint8_t *) malloc(gn);
                    memcpy(c, good, gn);
                    c[i] ^= (uint8_t) (1u << bit);
                    wifi_scan_result_t rr;
                    memset(&rr, 0xCC, sizeof rr); /* poison, so "untouched" shows */
                    if (wifi_parse_beacon(c, gn, -40, 2437, &rr) == WIFI_OK) {
                        bcn_ok++;
                        /* Whatever it ACCEPTED must satisfy the invariants the
                         * header promises, or the acceptance was a lie. */
                        if (strnlen(rr.ssid, sizeof rr.ssid) > 32) broke_invariant = 1;
                        if (!rr.hidden && rr.ssid[0] == '\0') broke_invariant = 1;
                        if (rr.hidden && rr.ssid[0] != '\0') broke_invariant = 1;
                        if ((int) rr.security < 0 || rr.security > WIFI_SEC_WPA2_WPA3)
                            broke_invariant = 1;
                        if ((int) rr.standard < 0 || rr.standard > WIFI_802_11BE)
                            broke_invariant = 1;
                        if ((int) rr.band < 0 || rr.band > WIFI_BAND_TRI) broke_invariant = 1;
                        if (rr.rssi != -40) broke_invariant = 1;
                    } else {
                        bcn_refused++;
                    }
                    free(c);
                    parsed++;
                }
        }

        /* The same treatment for an EAPOL-Key frame, driven all the way into
         * the supplicant so the handshake logic sees the corrupted frames too. */
        {
            uint8_t kd[22];
            uint8_t e[160];
            memset(kd, 0x5A, sizeof kd);
            uint32_t en = make_eapol(e, (uint16_t) (2 | WIFI_KI_PAIRWISE | WIFI_KI_ACK), 42, anonce,
                                     kd, 22, 0);
            for (uint32_t t = 0; t <= en; t++) {
                uint8_t *c = (uint8_t *) malloc(t ? t : 1);
                memcpy(c, e, t);
                wifi_eapol_key_t kk;
                uint8_t kck[16], mic[16];
                memset(kck, 0x22, 16);
                (void) wifi_eapol_key_parse(c, t, &kk);
                (void) wifi_eapol_mic(kck, c, t, 2, mic);
                (void) wifi_eapol_mic_verify(kck, c, t, 2);
                free(c);
                parsed++;
            }
            for (uint32_t i = 0; i < en; i++)
                for (int bit = 0; bit < 8; bit++) {
                    uint8_t *c = (uint8_t *) malloc(en);
                    memcpy(c, e, en);
                    c[i] ^= (uint8_t) (1u << bit);
                    wifi_eapol_key_t kk;
                    uint8_t reply[160];
                    uint32_t rl = 0;
                    memset(&kk, 0xCC, sizeof kk);
                    if (wifi_eapol_key_parse(c, en, &kk) == WIFI_OK) {
                        eap_ok++;
                        /* An accepted frame must fit inside the buffer it came
                         * from — total_len is what every later reader trusts. */
                        if (kk.total_len > en) broke_invariant = 1;
                        if (kk.total_len != 99u + kk.key_data_len) broke_invariant = 1;
                        if (kk.key_data_off != 99u) broke_invariant = 1;
                        if (kk.msg > 4) broke_invariant = 1;
                    } else {
                        eap_refused++;
                    }
                    (void) wifi_wpa_rx_eapol(&hd, hs, c, en, reply, sizeof reply, &rl);
                    free(c);
                    parsed++;
                }
        }

        printf("       %lu malformed/truncated inputs through every parser\n", parsed);
        printf("       corrupted beacons: %lu accepted / %lu refused;"
               " EAPOL: %lu / %lu\n",
               bcn_ok, bcn_refused, eap_ok, eap_refused);
        CHECK(parsed > 61000, "the hostile-input pass really ran");
        CHECK(!broke_invariant,
              "every input the parsers ACCEPTED satisfies the invariants the header promises "
              "(SSID <= 32 and consistent with `hidden`, enums in range, EAPOL total_len "
              "inside the buffer) — acceptance is never a lie");
        CHECK(bcn_ok > 0 && bcn_refused > 0,
              "single-bit corruption of a beacon produces BOTH acceptances and refusals — "
              "the parser is discriminating, not blanket-accepting or blanket-rejecting");
        CHECK(eap_ok > 0 && eap_refused > 0, "...and the same holds for EAPOL-Key frames");
        CHECK(wifi_get_scan_count(&hd) <= WIFI_MAX_SCAN_RESULTS,
              "the scan table never exceeds its bound, however much garbage it was fed");
    }

    /* ================================================================= */
    printf("\n--- interface table limits ---\n");
    /* ================================================================= */
    {
        wifi_device_t ld;
        wifi_init(&ld, "limits");
        uint32_t ids[WIFI_MAX_INTERFACES + 1];
        for (uint32_t i = 0; i < WIFI_MAX_INTERFACES; i++)
            ids[i] = wifi_create_interface(&ld, "w", WIFI_MODE_STATION);
        ids[WIFI_MAX_INTERFACES] = wifi_create_interface(&ld, "w", WIFI_MODE_STATION);
        CHECK(ids[0] == 1 && ids[WIFI_MAX_INTERFACES - 1] == WIFI_MAX_INTERFACES,
              "four interfaces get ids 1..4");
        CHECK(ids[WIFI_MAX_INTERFACES] == 0, "the fifth is refused with id 0");
        CHECK(ld.num_ifaces == WIFI_MAX_INTERFACES, "the count stops at 4");
        {
            /* Unsupported modes must be refused on their own merits. Asking a
             * FULL table proves nothing: every mode is refused there, so the
             * capability gate would look correct even if it were absent.
             * Ask a device with room to spare. */
            wifi_device_t md;
            wifi_init(&md, "modes-empty");
            CHECK(md.num_ifaces == 0 && !md.supports_mesh && !md.supports_monitor && md.supports_ap,
                  "a fresh device has room, cannot mesh or monitor, and can be an AP");
            CHECK(wifi_create_interface(&md, "m", WIFI_MODE_MESH) == 0,
                  "mesh mode is refused on an EMPTY device: supports_mesh is false and is "
                  "not pretended");
            CHECK(wifi_create_interface(&md, "mon", WIFI_MODE_MONITOR) == 0,
                  "monitor mode is refused the same way");
            CHECK(md.num_ifaces == 0, "...and neither refusal consumed an interface slot");
            CHECK(wifi_create_interface(&md, "ap0", WIFI_MODE_AP) == 1,
                  "while AP mode, which IS supported, is granted on the same device");
            CHECK(wifi_create_interface(&md, "x", (wifi_mode_t) 99) == 0,
                  "a mode outside the enum is refused rather than stored");
            CHECK(md.num_ifaces == 1, "the table holds exactly the one interface granted");
        }
        {
            /* Placeholder MACs are only useful if they are distinct: two
             * interfaces sharing an address would collide on the air. */
            wifi_device_t pd;
            wifi_init(&pd, "placeholders");
            uint32_t p1 = wifi_create_interface(&pd, "a", WIFI_MODE_STATION);
            uint32_t p2 = wifi_create_interface(&pd, "b", WIFI_MODE_STATION);
            uint32_t p3 = wifi_create_interface(&pd, "c", WIFI_MODE_STATION);
            const uint8_t *m1 = wifi_get_interface(&pd, p1)->mac;
            const uint8_t *m2 = wifi_get_interface(&pd, p2)->mac;
            const uint8_t *m3 = wifi_get_interface(&pd, p3)->mac;
            CHECK(memcmp(m1, m2, 6) != 0 && memcmp(m2, m3, 6) != 0 && memcmp(m1, m3, 6) != 0,
                  "three placeholder MACs on one device are three DIFFERENT addresses");
            CHECK((m1[0] & 0x03) == 0x02 && (m2[0] & 0x03) == 0x02 && (m3[0] & 0x03) == 0x02,
                  "...each locally administered (0x02) and unicast (not 0x01)");
            { /* deterministic across devices of the same name... */
                wifi_device_t pd2;
                wifi_init(&pd2, "placeholders");
                uint32_t q1 = wifi_create_interface(&pd2, "a", WIFI_MODE_STATION);
                CHECK(memcmp(wifi_get_interface(&pd2, q1)->mac, m1, 6) == 0,
                      "the same device name and index derive the SAME placeholder MAC");
                /* ...and different for a differently named device */
                wifi_device_t pd3;
                wifi_init(&pd3, "other-device");
                uint32_t s1 = wifi_create_interface(&pd3, "a", WIFI_MODE_STATION);
                CHECK(memcmp(wifi_get_interface(&pd3, s1)->mac, m1, 6) != 0,
                      "a different device name derives a different one");
            }
        }
        CHECK(!wifi_verify_coverage(&ld),
              "coverage fails: four coherent interfaces but no radio (l = 0)");
        {
            wifi_device_t ed;
            wifi_init(&ed, "empty");
            CHECK(!wifi_verify_coverage(&ed),
                  "a device with no interfaces and no radio does not pass coverage");
        }
        CHECK(!wifi_verify_coverage(0), "coverage of a NULL device is false, not a crash");
    }

    printf("\n%s: %d check(s), %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", checks,
           failures);
    return failures ? 1 : 0;
}
