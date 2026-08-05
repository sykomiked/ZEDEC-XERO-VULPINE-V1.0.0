/* vring.c — portable split-virtqueue engine. See vring.h. */
#include "vring.h"

bool vring_init(vring_t *vr, vring_desc_t *desc, vring_avail_t *avail,
                vring_used_t *used, uint16_t depth) {
    if (!vr || !desc || !avail || !used) return false;
    if (depth == 0 || depth > VRING_MAX_DEPTH) return false;
    vr->desc = desc; vr->avail = avail; vr->used = used;
    vr->depth = depth;

    /* free list threaded through desc[].next: 0 -> 1 -> ... -> depth-1 */
    for (uint16_t i = 0; i < depth; i++) {
        desc[i].addr = 0; desc[i].len = 0; desc[i].flags = 0;
        desc[i].next = (uint16_t)(i + 1);
    }
    vr->free_head = 0;
    vr->num_free = depth;

    avail->flags = 0; avail->idx = 0;
    used->flags = 0; used->idx = 0;
    vr->last_used = 0;
    return true;
}

uint16_t vring_num_free(const vring_t *vr) { return vr ? vr->num_free : 0; }

int32_t vring_add(vring_t *vr, const vring_buf_t *bufs, uint16_t n) {
    if (!vr || !bufs || n == 0 || n > vr->num_free) return -1;

    uint16_t head = vr->free_head;
    uint16_t prev = head;                 /* silences -Wmaybe; set on first iter */
    uint16_t cur = head;
    for (uint16_t i = 0; i < n; i++) {
        vring_desc_t *d = &vr->desc[cur];
        d->addr  = (uint64_t)bufs[i].addr;
        d->len   = bufs[i].len;
        d->flags = bufs[i].device_writable ? VRING_DESC_F_WRITE : 0;
        if (i + 1u < n) d->flags |= VRING_DESC_F_NEXT;   /* more to come */
        prev = cur;
        cur = d->next;                    /* advance the free list */
    }
    /* `cur` is now the new free-list head; detach the chain we took */
    vr->free_head = cur;
    vr->num_free  = (uint16_t)(vr->num_free - n);
    vr->desc[prev].next = 0;              /* terminate the used chain cleanly */

    /* publish the head in the avail ring, then advance idx (a release barrier
     * belongs between these two in the real driver; the caller does it). */
    vr->avail->ring[vr->avail->idx % vr->depth] = head;
    vr->avail->idx = (uint16_t)(vr->avail->idx + 1);
    return (int32_t)head;
}

int32_t vring_get_used(vring_t *vr, uint32_t *len_out) {
    if (!vr) return -1;
    if (vr->used->idx == vr->last_used) return -1;      /* nothing new */

    const vring_used_elem_t *e = &vr->used->ring[vr->last_used % vr->depth];
    uint16_t head = (uint16_t)e->id;
    if (len_out) *len_out = e->len;

    /* walk the chain to count it and find its tail, then splice the whole
     * run back onto the free list in O(chain) */
    uint16_t cur = head, tail = head, count = 1;
    while (vr->desc[cur].flags & VRING_DESC_F_NEXT) {
        cur = vr->desc[cur].next;
        tail = cur;
        count++;
        if (count > vr->depth) break;      /* corrupt chain guard */
    }
    vr->desc[tail].next = vr->free_head;
    vr->free_head = head;
    vr->num_free = (uint16_t)(vr->num_free + count);

    vr->last_used = (uint16_t)(vr->last_used + 1);
    return (int32_t)head;
}
