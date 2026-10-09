/* test_vring.c — the split-virtqueue engine against a mock device.
 *
 * A mock device plays the other side of the ring: it reads the avail ring,
 * "processes" each descriptor chain (loopback — sums the byte lengths), and
 * publishes a used entry. Driving thousands of submit/reap cycles at a small
 * depth exercises the free-list and BOTH ring wrap-arounds, which is exactly
 * where these engines break.
 */
#include <stdio.h>
#include <string.h>
#include "vring.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* ---- mock device ---- */
typedef struct { uint16_t seen_avail; } mock_dev_t;

/* Process every newly-published avail entry: read the chain, compute its
 * total length, and complete it in the used ring (FIFO, like QEMU). */
static uint32_t mock_process(vring_t *vr, mock_dev_t *dev) {
    uint32_t done = 0;
    while (dev->seen_avail != vr->avail->idx) {
        uint16_t head = vr->avail->ring[dev->seen_avail % vr->depth];
        uint32_t total = 0;
        uint16_t cur = head, guard = 0;
        for (;;) {
            total += vr->desc[cur].len;
            if (!(vr->desc[cur].flags & VRING_DESC_F_NEXT)) break;
            cur = vr->desc[cur].next;
            if (++guard > vr->depth) break;
        }
        vr->used->ring[vr->used->idx % vr->depth].id = head;
        vr->used->ring[vr->used->idx % vr->depth].len = total;
        vr->used->idx = (uint16_t)(vr->used->idx + 1);
        dev->seen_avail = (uint16_t)(dev->seen_avail + 1);
        done++;
    }
    return done;
}

int main(void) {
    printf("=== split-virtqueue engine (vring) ===\n");
    enum { D = 8 };
    static vring_desc_t desc[VRING_MAX_DEPTH];
    static vring_avail_t avail;
    static vring_used_t used;
    vring_t vr;
    mock_dev_t dev = {0};

    CHECK(vring_init(&vr, desc, &avail, &used, D), "engine binds to the rings");
    CHECK(vring_num_free(&vr) == D, "all descriptors free at init");
    CHECK(!vring_init(&vr, desc, &avail, &used, VRING_MAX_DEPTH + 1), "over-depth refused");

    /* single-buffer submit/complete round trip */
    uint8_t b0[100];
    vring_buf_t one = { b0, sizeof b0, false };
    int32_t h = vring_add(&vr, &one, 1);
    CHECK(h >= 0 && vring_num_free(&vr) == D - 1, "one chain submitted, one desc used");
    mock_process(&vr, &dev);
    uint32_t len = 0;
    int32_t got = vring_get_used(&vr, &len);
    CHECK(got == h && len == sizeof b0, "reaped the same head, device-reported length correct");
    CHECK(vring_num_free(&vr) == D, "descriptor returned to the free list");

    /* a 3-descriptor chain (e.g. virtio-net header + frame + trailer) */
    uint8_t p1[14], p2[46], p3[4];
    vring_buf_t three[3] = { {p1,14,false}, {p2,46,false}, {p3,4,true} };
    h = vring_add(&vr, three, 3);
    CHECK(h >= 0 && vring_num_free(&vr) == D - 3, "3-buffer chain uses 3 descriptors");
    /* the middle descriptor must carry NEXT; the last must not; WRITE set on p3 */
    CHECK((desc[h].flags & VRING_DESC_F_NEXT) && !(desc[h].flags & VRING_DESC_F_WRITE),
          "head chains and is device-readable");
    uint16_t mid = desc[h].next, tail = desc[mid].next;
    CHECK((desc[mid].flags & VRING_DESC_F_NEXT) &&
          !(desc[tail].flags & VRING_DESC_F_NEXT) &&
          (desc[tail].flags & VRING_DESC_F_WRITE),
          "tail terminates the chain and is device-writable (an RX buffer)");
    mock_process(&vr, &dev);
    got = vring_get_used(&vr, &len);
    CHECK(got == h && len == 14 + 46 + 4, "3-chain reaped; total length summed");
    CHECK(vring_num_free(&vr) == D, "all 3 descriptors freed");

    /* exhaustion: cannot submit more than the ring holds */
    CHECK(vring_add(&vr, three, 3) >= 0, "fill 3");
    CHECK(vring_add(&vr, three, 3) >= 0, "fill 6");
    CHECK(vring_add(&vr, three, 3) < 0, "the 9th of 8 descriptors is refused (no overrun)");
    CHECK(vring_get_used(&vr, &len) < 0, "nothing reapable until the device processes");
    mock_process(&vr, &dev);
    CHECK(vring_get_used(&vr, &len) >= 0 && vring_get_used(&vr, &len) >= 0,
          "after processing, the submitted chains reap");
    while (vring_get_used(&vr, &len) >= 0) { }        /* drain */
    CHECK(vring_num_free(&vr) == D, "ring fully drained back to empty");

    /* ---- the stress: thousands of cycles across BOTH wrap points ---- */
    {
        uint8_t scratch[64];
        vring_buf_t bufs[2] = { {scratch, 10, false}, {scratch, 20, true} };
        uint32_t submitted = 0, reaped = 0;
        for (uint32_t round = 0; round < 5000; round++) {
            /* submit as many 2-desc chains as fit, then let the device run */
            while (vring_num_free(&vr) >= 2) { if (vring_add(&vr, bufs, 2) < 0) break; submitted++; }
            mock_process(&vr, &dev);
            while (vring_get_used(&vr, &len) >= 0) { reaped++; }
        }
        printf("       stress: %u submitted, %u reaped, %u free\n",
               submitted, reaped, vring_num_free(&vr));
        CHECK(submitted == reaped && submitted > 10000,
              "every chain submitted was reaped exactly once over 5000 rounds");
        CHECK(vring_num_free(&vr) == D,
              "the free list is intact after >20000 descriptors cycled through the wrap");
        CHECK(avail.idx == used.idx,
              "avail and used indices stayed in lockstep (no lost or double completion)");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
