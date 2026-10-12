/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sensor_demo.c — a soil-moisture node with two redundant probes logs a
 * reading every 15 minutes into a 32-reading log (fixed storage, no heap).
 * Every hour it sends what is waiting at the data rate the network gives it
 * (EU868 DR0 at the field's edge, DR5 near the gateway). The gateway
 * forwards each payload to the mesh as UBH-168 frames and accepts batches
 * strictly in order. One uplink is lost, a reply is lost so the node sends
 * readings the gateway already holds (refused as a duplicate, and the reply
 * resynchronises the node), and an old payload replayed later is refused.
 * When the probes disagree, the reading is BOTH with both values kept. */
#include <stdio.h>
#include <string.h>

#include "zvband.h"

static void refused(void *user, uint32_t device, int32_t st, uint32_t batch) {
    (void) user;
    printf("    gateway refuses device 0x%08X batch %u: %s\n", device, batch, zvb_status_name(st));
}

int main(void) {
    static zvb_record storage[32], got[64];
    static const char *lp[4] = {"NEITHER", "TRUE", "FALSE", "BOTH"};
    zvb_log log;
    zvb_gw gw;
    zvb_gw_dev devs[4];
    uint8_t payload[ZVB_BATCH_MAX], old[ZVB_BATCH_MAX], frames[ZVB_WIRE_MAX_FRAMES * ZXV_WIRE_FRAME_BYTES];
    uint8_t rx[ZVB_BATCH_MAX];
    size_t len = 0, oldlen = 0, nf = 0, rl = 0;
    uint32_t t = 1760000000u, held = 0;

    zvb_log_init(&log, storage, 32, 0x00A1B2C3u);
    zvb_gw_init(&gw, devs, 4, refused, NULL);
    zvb_gw_add(&gw, 0x00A1B2C3u);

    for (int hour = 0; hour < 6; hour++) {
        uint8_t dr = (hour % 3 == 0) ? 0 : 5;
        uint32_t max = 0, count = 0, next = 0;
        zvb_batch_info info;
        int32_t st;
        for (int q = 0; q < 4; q++) {
            int32_t a = 3120 + 7 * hour + q, b = a + ((hour == 2 && q == 1) ? 410 : (q & 1));
            t += 900;
            if (hour == 4 && q == 3) zvb_log_append(&log, 1, t, ZXV_LP_FALSE, 17); /* probe fault code */
            else zvb_log_append_pair(&log, 1, t, a, b, 3);                        /* per mille moisture */
        }
        zvb_payload_max(ZVB_REGION_EU868, dr, &max);
        st = zvb_batch_make(&log, max, payload, sizeof payload, &len, &count);
        printf("hour %d: EU868 DR%u (max %u bytes): batch of %u readings in %zu bytes (%s)\n", hour, dr,
               max, count, len, zvb_status_name(st));
        if (st != ZXV_OK) continue;
        if (hour == 1) {
            printf("    uplink lost; the node queues the readings again\n");
            memcpy(old, payload, len);
            oldlen = len;
            zvb_log_ack(&log, log.acked + 1);
            continue;
        }
        zvb_wire_encode(2, payload, len, frames, sizeof frames, &nf);
        zvb_wire_decode(frames, nf, rx, sizeof rx, &rl);
        printf("    forwarded as %zu UBH-168 frames\n", nf);
        st = zvb_gw_accept(&gw, rx, rl, got, 64, &info, &next);
        if (st == ZXV_OK) {
            for (uint32_t i = 0; i < info.count; i++) {
                if (got[i].lp == ZXV_LP_BOTH)
                    printf("    seq %u: BOTH, probes read %d and %d (held for a person)\n", got[i].seq,
                           got[i].value, got[i].value2);
                else if (got[i].lp != ZXV_LP_TRUE)
                    printf("    seq %u: %s, code %d\n", got[i].seq, lp[got[i].lp], got[i].value);
            }
            held += info.count;
            printf("    accepted readings %u..%u, batch tag %s\n", info.first_seq,
                   info.first_seq + info.count - 1, zxv_hex_name(info.tag));
        }
        if (hour == 3) {
            printf("    the gateway's reply is lost\n");
            zvb_log_ack(&log, log.acked + 1);
            continue;
        }
        printf("    reply: next expected %u\n", next);
        zvb_log_ack(&log, next);
    }
    {
        uint32_t next = 0;
        printf("an old payload replayed:\n");
        zvb_gw_accept(&gw, old, oldlen, got, 64, NULL, &next);
    }
    printf("gateway holds %u readings (node logged %u); device %u accepted, %u refused\n", held,
           log.next_seq - 1, devs[0].accepted, devs[0].refused);
    return held == log.next_seq - 1 ? 0 : 1;
}
