/* zorder.c — fractal (self-similar) component addressing. See zorder.h. */
#include "zorder.h"

/* ---- Morton interleave: spread 16 bits so they occupy the even slots ---- */
static uint32_t spread16(uint16_t v) {
    uint32_t x = v;
    x = (x | (x << 8)) & 0x00FF00FFu;
    x = (x | (x << 4)) & 0x0F0F0F0Fu;
    x = (x | (x << 2)) & 0x33333333u;
    x = (x | (x << 1)) & 0x55555555u;
    return x;
}
static uint16_t compact16(uint32_t x) {
    x &= 0x55555555u;
    x = (x | (x >> 1)) & 0x33333333u;
    x = (x | (x >> 2)) & 0x0F0F0F0Fu;
    x = (x | (x >> 4)) & 0x00FF00FFu;
    x = (x | (x >> 8)) & 0x0000FFFFu;
    return (uint16_t)x;
}

uint32_t zo_encode2(uint16_t x, uint16_t y) {
    return spread16(x) | (spread16(y) << 1);
}
void zo_decode2(uint32_t code, uint16_t *x, uint16_t *y) {
    if (x) *x = compact16(code);
    if (y) *y = compact16(code >> 1);
}

zo_addr_t zo_make(uint16_t x, uint16_t y, uint8_t level) {
    zo_addr_t a;
    if (level > ZO_MAX_LEVEL) level = ZO_MAX_LEVEL;
    a.level = level;
    a.code = zo_encode2(x, y);
    return a;
}

/* For an address at level L the Morton code occupies the low 2L bits
 * (coordinates are < 2^L). The prefix naming the enclosing region at
 * level k is therefore code >> ((L-k)*2) — relative to the ADDRESS'S OWN
 * level, not to ZO_MAX_LEVEL.
 *
 * (Shifting relative to ZO_MAX_LEVEL was the original bug: for small
 * coordinates every prefix collapsed to 0, so every pair of components
 * looked co-located and all hop costs measured zero.) */
static uint32_t prefix_at(zo_addr_t a, uint8_t k) {
    if (k >= a.level) return a.code;
    uint32_t drop = (uint32_t)(a.level - k) * 2u;
    return (drop >= 32) ? 0u : (a.code >> drop);
}

zo_addr_t zo_parent(zo_addr_t a, uint8_t up) {
    zo_addr_t p;
    p.level = (a.level > up) ? (uint8_t)(a.level - up) : 0;
    p.code = prefix_at(a, p.level);
    return p;
}

bool zo_contains(zo_addr_t outer, zo_addr_t inner) {
    if (outer.level > inner.level) return false;
    return outer.code == prefix_at(inner, outer.level);
}

uint8_t zo_common_level(zo_addr_t a, zo_addr_t b) {
    uint8_t lim = (a.level < b.level) ? a.level : b.level;
    for (uint8_t l = lim; l > 0; l--) {
        if (prefix_at(a, l) == prefix_at(b, l)) return l;
    }
    return 0;
}

uint32_t zo_hops(zo_addr_t a, zo_addr_t b) {
    if (a.code == b.code && a.level == b.level) return 0;
    uint8_t lim = (a.level < b.level) ? a.level : b.level;
    uint8_t common = zo_common_level(a, b);
    /* climb from a to the common ancestor, then descend to b */
    return (uint32_t)((lim - common) * 2u);
}

static uint32_t uabs(int32_t v) { return (v < 0) ? (uint32_t)(-v) : (uint32_t)v; }

uint32_t zo_manhattan(zo_addr_t a, zo_addr_t b) {
    uint16_t ax, ay, bx, by;
    zo_decode2(a.code, &ax, &ay);
    zo_decode2(b.code, &bx, &by);
    return uabs((int32_t)ax - (int32_t)bx) + uabs((int32_t)ay - (int32_t)by);
}

uint64_t zo_cost(const uint16_t *affinity, uint32_t count,
                 const uint16_t *slot_x, const uint16_t *slot_y,
                 uint8_t level) {
    if (!affinity || !slot_x || !slot_y) return 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            uint16_t w = affinity[i * count + j];
            if (!w) continue;
            zo_addr_t a = zo_make(slot_x[i], slot_y[i], level);
            zo_addr_t b = zo_make(slot_x[j], slot_y[j], level);
            total += (uint64_t)w * (uint64_t)zo_hops(a, b);
        }
    }
    return total;
}

/* Greedy locality placement.
 *
 * Walk slots in Z-ORDER (which visits the grid so that consecutive slots
 * stay inside the same quadrant for as long as possible), and fill them
 * by repeatedly taking the unplaced component with the strongest
 * affinity to what has already been placed. Heavy communicators are
 * therefore emitted as a run of consecutive Z-order slots, which puts
 * them inside a shared low-level region — exactly the property that
 * makes zo_hops small.
 *
 * This is a heuristic, not an optimum; the test measures what it
 * actually achieves rather than assuming a win. */
uint64_t zo_place(const uint16_t *affinity, uint32_t count,
                  uint8_t level, uint16_t *slot_x, uint16_t *slot_y) {
    if (!affinity || !slot_x || !slot_y || count == 0) return 0;

    /* bounded: this module never allocates */
    #define ZO_PLACE_MAX 256
    if (count > ZO_PLACE_MAX) count = ZO_PLACE_MAX;
    bool placed[ZO_PLACE_MAX];
    uint32_t order[ZO_PLACE_MAX];
    for (uint32_t i = 0; i < count; i++) placed[i] = false;

    /* seed with the component that has the largest total affinity */
    uint32_t seed = 0; uint64_t best = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t s = 0;
        for (uint32_t j = 0; j < count; j++)
            s += affinity[i * count + j] + affinity[j * count + i];
        if (s > best) { best = s; seed = i; }
    }
    order[0] = seed; placed[seed] = true;

    for (uint32_t n = 1; n < count; n++) {
        uint32_t pick = 0; uint64_t pbest = 0; bool found = false;
        for (uint32_t c = 0; c < count; c++) {
            if (placed[c]) continue;
            uint64_t tie = 0;
            for (uint32_t k = 0; k < n; k++) {
                uint32_t o = order[k];
                tie += affinity[c * count + o] + affinity[o * count + c];
            }
            if (!found || tie > pbest) { pbest = tie; pick = c; found = true; }
        }
        order[n] = pick; placed[pick] = true;
    }

    /* Emit the ordered components into consecutive Z-order slots. */
    uint32_t side = 1u << level;
    for (uint32_t n = 0; n < count; n++) {
        /* the n-th slot in Z-order is simply Morton code n, decoded */
        uint16_t sx, sy;
        zo_decode2((uint32_t)n, &sx, &sy);
        if (sx >= side) sx = (uint16_t)(side - 1);
        if (sy >= side) sy = (uint16_t)(side - 1);
        slot_x[order[n]] = sx;
        slot_y[order[n]] = sy;
    }
    return zo_cost(affinity, count, slot_x, slot_y, level);
}
