/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* axioms_alloc.c — Tier 1 root axioms for the freestanding allocators:
 *   kernel/include/freestanding.h  fs_malloc / fs_calloc (bump allocator)
 *   kernel/src/mm                  kmalloc / kfree / kcalloc / krealloc and the
 *                                  page-frame bitmap (mm_alloc_frame/mm_free_frame)
 *   kernel/src/audio               the stream ring buffers and the shared pool
 * Each pool is driven to exactly 100% and one past it. A full pool must fail
 * closed (NULL / 0 / not present) and leave its accounting unchanged; every
 * block handed out and given back must return the accounting to where it was.
 *
 * mm's heap lives at the fixed kernel address KERNEL_HEAP_BASE (0x300000); the
 * hosted test maps one page there with MAP_FIXED_NOREPLACE before mm_init, so
 * the allocator runs unmodified.
 */
#include "tier.h"
#include <sys/mman.h>
/* the module headers first: they pull in <math.h>, which freestanding.h's
 * sqrt/exp/... macros would otherwise rename */
#include "mm.h"
#include "rmag_core.h"
#include "audio.h"
#include "freestanding.h"
#undef printf
#undef snprintf
#undef assert

void *fs_heap_peer_alloc(size_t n);

static const tier_known_t KNOWN_FAILURES[] = {
    {"F-FS-ALIGN", "fs_malloc returns pointers with no alignment guarantee"},
    {"F-FS-PER-TU", "fs_malloc's heap is a static inside a static inline function: every "
                    "translation unit gets its own private 1 MiB heap"},
    {"F-MM-DFREE", "kfree of an already-free block corrupts heap_used (no double-free guard)"},
    {"F-MM-FRAME0", "frame index 0 doubles as 'no frame': it can be allocated but never freed"},
    {"F-MM-QUOTA", "mm_free_frame zeroes page->frame before reading the RMAG quota, so the "
                   "freed frame's quota is charged to frame 0"},
    {"F-MM-ALIGN", "kmalloc rounds sizes to 4 bytes, so on a 64-bit build the next block header "
                   "is misaligned (UB)"},
    {"F-MM-SHIFT31", "the frame bitmap shifts the int 1 by 31 (signed overflow, UB)"},
};

/* ===== fs_malloc / fs_calloc ===== */
#define FS_HEAP (1024u * 1024u)

static void axiom_fs_malloc(void)
{
    size_t used = 0;
    /* MAX+1 and SIZE_MAX first: refused, nothing consumed */
    CHECK(fs_malloc(FS_HEAP + 1) == NULL, "fs_malloc(MAX+1) refused");
    CHECK(fs_malloc((size_t) -1) == NULL, "fs_malloc(SIZE_MAX) refused (no wrap)");
    CHECK(fs_calloc((size_t) -1 / 2 + 1, 2) == NULL, "fs_calloc count*size overflow refused");
    CHECK(fs_calloc(2, (size_t) -1 / 2 + 1) == NULL, "fs_calloc size*count overflow refused");
    /* alignment: one odd-sized block, then an 8-byte object */
    unsigned char *odd = fs_malloc(1);
    used += 1;
    uint64_t *q = fs_malloc(sizeof *q);
    used += sizeof *q;
    CHECK(odd != NULL && q != NULL, "small blocks");
    CHECK_KNOWN("F-FS-ALIGN", ((uintptr_t) q % _Alignof(max_align_t)) == 0,
                "fs_malloc(8) after fs_malloc(1) is aligned to %zu (got addr %% 16 = %zu)",
                (size_t) _Alignof(max_align_t), (size_t) ((uintptr_t) q % 16));
    CHECK(q == (uint64_t *) (odd + 1), "bump: blocks are contiguous");
    /* calloc zeroes (the region is fresh .bss, so this is a weak check) */
    unsigned char *z = fs_calloc(16, 4);
    used += 64;
    bool zero = z != NULL;
    for (int i = 0; zero && i < 64; i++) zero = z[i] == 0;
    CHECK(zero, "fs_calloc(16, 4) zeroed");
    CHECK(fs_calloc(0, 4) != NULL && fs_calloc(4, 0) != NULL, "zero-size calloc is not an error");
    /* fill to exactly 100%: MAX-1 more bytes, then 1, then nothing */
    size_t left = FS_HEAP - used;
    CHECK(fs_malloc(left + 1) == NULL, "remaining+1 refused");
    CHECK(fs_malloc(left - 1) != NULL, "remaining-1 granted");
    CHECK(fs_malloc(2) == NULL, "2 bytes when 1 is left refused");
    unsigned char *last = fs_malloc(1);
    CHECK(last != NULL && last == (unsigned char *) odd + FS_HEAP - 1,
          "the very last byte is the heap's last byte");
    CHECK(fs_malloc(1) == NULL && fs_calloc(1, 1) == NULL, "full heap fails closed");
    /* another translation unit including the same header */
    void *peer = fs_heap_peer_alloc(16);
    CHECK_KNOWN("F-FS-PER-TU", peer == NULL,
                "this unit's heap is full, yet another unit still allocates (%p): one heap per "
                "translation unit",
                peer);
}

/* ===== mm ===== */
static mm_state_t MM;

static bool map_heap(void)
{
    void *want = (void *) (uintptr_t) KERNEL_HEAP_BASE;
    void *p = mmap(want, PAGE_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p == want;
}

#define HDR        ((uint32_t) sizeof(heap_block_t))
#define HEAP_FREE0 (PAGE_SIZE - HDR) /* the single free block after mm_init */

static uint32_t free_bytes(void)
{
    uint32_t n = 0;
    for (heap_block_t *b = MM.heap_head; b; b = b->next)
        if (b->free) n += b->size;
    return n;
}
static uint32_t n_blocks(void)
{
    uint32_t n = 0;
    for (heap_block_t *b = MM.heap_head; b; b = b->next) n++;
    return n;
}

static void axiom_kmalloc(void)
{
    mm_init(&MM);
    /* size boundaries: 0, HEAP_MAX, HEAP_MAX+1, UINT32_MAX */
    uint32_t bad[] = {0, HEAP_MAX + 1, UINT32_MAX - 2, UINT32_MAX};
    for (unsigned i = 0; i < TIER_N(bad); i++)
        CHECK(kmalloc(&MM, bad[i]) == NULL && MM.heap_used == 0, "kmalloc(%u) refused", bad[i]);
    CHECK(kmalloc(&MM, HEAP_MAX) == NULL && MM.heap_used == 0,
          "kmalloc(HEAP_MAX) larger than the heap refused");
    /* the largest grant: a block must leave room for a header (kmalloc rule) */
    uint32_t big = HEAP_FREE0 - HDR;
    CHECK(kmalloc(&MM, big + 1) == NULL && MM.heap_used == 0, "largest+1 refused");
    void *p = kmalloc(&MM, big);
    CHECK(p != NULL && MM.heap_used == PAGE_SIZE, "largest block: heap at 100%%");
    CHECK(kmalloc(&MM, 1) == NULL && MM.heap_used == PAGE_SIZE, "full heap fails closed");
    kfree(&MM, p);
    CHECK(MM.heap_used == 0 && n_blocks() == 1 && free_bytes() == HEAP_FREE0,
          "free returns the heap to one block");
    /* many small blocks to exhaustion, freed in an interleaved order */
    void *blk[256];
    uint32_t n = 0;
    while (n < 256 && (blk[n] = kmalloc(&MM, 13)) != NULL) {
        memset(blk[n], 0xA0 + (int) (n & 15), 13);
        n++;
    }
    CHECK(n > 10 && kmalloc(&MM, 13) == NULL, "exhausted after %u blocks", n);
    bool intact = true;
    for (uint32_t i = 0; i < n; i++)
        for (int k = 0; k < 13; k++)
            intact &= ((uint8_t *) blk[i])[k] == (uint8_t) (0xA0 + (i & 15));
    CHECK(intact, "no block overlaps another");
    for (uint32_t i = 0; i < n; i += 2) kfree(&MM, blk[i]);
    for (uint32_t i = 1; i < n; i += 2) kfree(&MM, blk[i]);
    CHECK(MM.heap_used == 0 && n_blocks() == 1 && free_bytes() == HEAP_FREE0,
          "interleaved frees coalesce back to one block (%u blocks, %u free)", n_blocks(),
          free_bytes());
    CHECK(kmalloc(&MM, big) != NULL, "largest block available again: no fragmentation leak");
    mm_init(&MM);
    kfree(&MM, NULL);
    CHECK(MM.heap_used == 0, "kfree(NULL) is a no-op");
    /* kcalloc: overflow, zeroing of REUSED memory */
    CHECK(kcalloc(&MM, 0x10000, 0x10000) == NULL, "kcalloc 32-bit overflow refused");
    uint8_t *d = kmalloc(&MM, 64);
    memset(d, 0xFF, 64);
    kfree(&MM, d);
    uint8_t *c = kcalloc(&MM, 16, 4);
    bool zero = c != NULL;
    for (int i = 0; zero && i < 64; i++) zero = c[i] == 0;
    CHECK(c == d && zero, "kcalloc zeroes a reused block");
    /* krealloc: grow keeps the bytes, shrink keeps the pointer, 0 frees */
    for (int i = 0; i < 64; i++) c[i] = (uint8_t) i;
    uint8_t *g = krealloc(&MM, c, 200);
    bool kept = g != NULL;
    for (int i = 0; kept && i < 64; i++) kept = g[i] == (uint8_t) i;
    CHECK(kept, "krealloc grow preserves contents");
    CHECK(krealloc(&MM, g, 8) == g, "krealloc shrink keeps the block");
    CHECK(krealloc(&MM, g, 0) == NULL && MM.heap_used == 0, "krealloc(p, 0) frees");
    CHECK(krealloc(&MM, NULL, 0) == NULL, "krealloc(NULL, 0)");
    CHECK(krealloc(&MM, NULL, PAGE_SIZE * 2) == NULL && MM.heap_used == 0,
          "krealloc beyond the heap refused");
    /* double free */
    uint8_t *x = kmalloc(&MM, 32), *y = kmalloc(&MM, 32);
    (void) y;
    kfree(&MM, x);
    uint32_t used = MM.heap_used;
    kfree(&MM, x);
    CHECK_KNOWN("F-MM-DFREE", MM.heap_used == used,
                "a second kfree of the same block changed heap_used %u -> %u", used, MM.heap_used);
}

/* heap list integrity: prev links mirror next links, and the accounting
 * (used blocks + free blocks + one header per block) covers the page */
static bool list_ok(void)
{
    uint32_t used = 0, total = 0;
    heap_block_t *prev = NULL;
    for (heap_block_t *b = MM.heap_head; b; b = b->next) {
        if (b->prev != prev) return false;
        if (!b->free) used += b->size + HDR;
        total += b->size + HDR;
        prev = b;
    }
    return used == MM.heap_used && total == PAGE_SIZE;
}

static void axiom_mm_more(void)
{
    /* mm_init clears every bitmap word, including the last one */
    MM.frames_bitmap[0] = MM.frames_bitmap[MAX_PAGES / 32 - 1] = 0xFFFFFFFFu;
    mm_init(&MM);
    bool clear = true;
    for (uint32_t i = 0; i < MAX_PAGES / 32; i++) clear &= MM.frames_bitmap[i] == 0;
    CHECK(clear && MM.used_pages == 0 && MM.total_pages == MAX_PAGES, "mm_init clears the bitmap");
    CHECK(MM.heap_end - MM.heap_start == PAGE_SIZE && MM.heap_start == KERNEL_HEAP_BASE,
          "heap bounds are one page at KERNEL_HEAP_BASE");
    CHECK(mm_get_total_memory() == (uint32_t) MAX_PAGES * PAGE_SIZE, "total memory");

    /* split threshold: a block is split only when the rest can hold a header
     * and more: exactly size + 2 headers is NOT split, 8 bytes less is (8, not
     * 4: a 4-byte step misaligns the next header on 64-bit, see F-MM-ALIGN) */
    uint32_t s = HEAP_FREE0 - 2 * HDR;
    void *p = kmalloc(&MM, s);
    CHECK(p && n_blocks() == 1 && MM.heap_used == PAGE_SIZE && list_ok(),
          "no split at size + 2 headers");
    kfree(&MM, p);
    p = kmalloc(&MM, s - 8);
    CHECK(p && n_blocks() == 2 && list_ok(), "split at size + 2 headers + 8");
    kfree(&MM, p);
    CHECK(n_blocks() == 1 && list_ok(), "back to one block");

    /* block headers stay aligned for their pointer members */
    TIER_UB_KNOWN("F-MM-ALIGN", ok, {
        mm_init(&MM);
        void *x = kmalloc(&MM, 4);
        void *y = kmalloc(&MM, 8);
        ok = x && y && ((uintptr_t) y % _Alignof(heap_block_t)) == 0;
    });
    mm_init(&MM);

    /* prev links through a split of a middle block and a two-way coalesce */
    uint8_t *a = kmalloc(&MM, 64), *b = kmalloc(&MM, 32), *c = kmalloc(&MM, 32),
            *d = kmalloc(&MM, 32);
    CHECK(a && b && c && d && list_ok(), "four blocks");
    kfree(&MM, a);
    uint8_t *a2 = kmalloc(&MM, 16); /* splits the freed first block, whose next is b */
    CHECK(a2 == a && list_ok(), "split of a block with a successor keeps the links");
    kfree(&MM, c);
    kfree(&MM, b); /* b merges with c: d->prev must become b */
    CHECK(list_ok(), "coalesce with the next block relinks its successor");
    kfree(&MM, a2);
    kfree(&MM, d);
    CHECK(n_blocks() == 1 && MM.heap_used == 0 && list_ok(), "all freed: one block");

    /* kcalloc: too large (no overflow) fails closed; zeroing stays inside the block */
    CHECK(kcalloc(&MM, 1, PAGE_SIZE * 2) == NULL && MM.heap_used == 0, "kcalloc too large");
    uint8_t *z = kcalloc(&MM, 16, 4);
    CHECK(z && list_ok(), "kcalloc writes nothing past its block");

    /* krealloc: grows really grow, same size keeps the block, too large keeps the original */
    for (int i = 0; i < 64; i++) z[i] = (uint8_t) (i + 1);
    CHECK(krealloc(&MM, z, 64) == z, "krealloc to the same size keeps the block");
    uint32_t used = MM.heap_used;
    CHECK(krealloc(&MM, z, PAGE_SIZE * 2) == NULL && MM.heap_used == used && z[63] == 64,
          "krealloc beyond the heap fails and keeps the original block");
    uint8_t *g = krealloc(&MM, z, 304);
    CHECK(g && ((heap_block_t *) (g - HDR))->size >= 304 && g[0] == 1 && g[63] == 64 && list_ok(),
          "krealloc grow gives a block at least the new size");
    kfree(&MM, g);

    /* frames: a page that already holds a frame is not given another; a freed
     * nonzero frame decrements the count and clears the page */
    mm_init(&MM);
    page_t f0 = {0}, f1 = {0};
    mm_alloc_frame(&MM, &f0, true, true); /* frame 0 (see F-MM-FRAME0) */
    mm_alloc_frame(&MM, &f1, true, true);
    uint32_t fr = f1.frame, up = MM.used_pages;
    mm_alloc_frame(&MM, &f1, true, true);
    CHECK(fr != 0 && f1.frame == fr && MM.used_pages == up, "a page holding frame %u is left alone",
          fr);
    CHECK(f1.rw && !f1.user, "kernel writable page flags");
    mm_free_frame(&MM, &f1);
    CHECK(MM.used_pages == up - 1 && f1.frame == 0 && !f1.present, "freeing frame %u", fr);
    page_t f2 = {0};
    mm_alloc_frame(&MM, &f2, false, false);
    CHECK(f2.frame == fr && !f2.rw && f2.user, "the freed frame is reused; user read-only flags");

    /* the exact rational used-memory total is the sum of the frame quotas */
    int64_t sum = 0;
    bool den1 = true;
    for (uint32_t i = 0; i < MAX_PAGES; i++) {
        rational_t q = rmag_get_quota(i);
        den1 &= q.den == 1;
        sum += q.num;
    }
    rational_t u = mm_get_used_rational();
    CHECK(den1 && u.num == sum && u.den == 1, "used rational == sum of quotas (%lld)",
          (long long) sum);

    /* mm_get_page: present table -> its entry; absent table -> NULL */
    static page_directory_t dir;
    static page_table_t tab;
    memset(&dir, 0, sizeof dir);
    dir.tables[2] = &tab;
    CHECK(mm_get_page(&MM, (2u * 1024u + 5u) * PAGE_SIZE, &dir, false) == &tab.pages[5] &&
              mm_get_page(&MM, (2u * 1024u + 1023u) * PAGE_SIZE + 7u, &dir, false) ==
                  &tab.pages[1023],
          "mm_get_page finds the entry");
    CHECK(mm_get_page(&MM, 3u * 1024u * PAGE_SIZE, &dir, false) == NULL &&
              mm_get_page(&MM, 1u * 1024u * PAGE_SIZE, &dir, true) == NULL,
          "mm_get_page on an absent table");
}

static void axiom_frames(void)
{
    mm_init(&MM);
    rmag_init(64);
    page_t p0 = {0}, p1 = {0};
    mm_alloc_frame(&MM, &p0, true, true);
    CHECK(p0.present && MM.used_pages == 1, "first frame allocated");
    uint32_t got = p0.frame;
    mm_free_frame(&MM, &p0);
    CHECK_KNOWN("F-MM-FRAME0", MM.used_pages == 0 && !p0.present,
                "allocated frame %u, freed it: used_pages=%u present=%d", got, MM.used_pages,
                (int) p0.present);
    mm_alloc_frame(&MM, &p0, true, true); /* p0.frame is 0 again: looks unallocated */
    CHECK_KNOWN("F-MM-FRAME0", MM.used_pages <= 1,
                "re-allocating a page that holds frame 0 counted again: used_pages=%u",
                MM.used_pages);
    /* RMAG quota follows the frame */
    mm_alloc_frame(&MM, &p1, true, true);
    uint32_t f1 = p1.frame;
    rational_t q = rmag_get_quota(f1);
    CHECK(f1 != 0 && q.num == PAGE_SIZE && q.den == 1, "quota of frame %u = one page", f1);
    mm_free_frame(&MM, &p1);
    q = rmag_get_quota(f1);
    CHECK_KNOWN("F-MM-QUOTA", q.num == 0, "after freeing frame %u its quota is %lld/%lld", f1,
                (long long) q.num, (long long) q.den);
    CHECK(MM.used_pages + (uint32_t) (mm_get_free_memory(&MM) / PAGE_SIZE) == MAX_PAGES,
          "used + free pages = MAX_PAGES");
}

static void alloc_64_frames(void *u)
{
    (void) u;
    static mm_state_t m;
    mm_init(&m);
    for (int i = 0; i < 64; i++) {
        page_t p = {0};
        p.frame = 0;
        mm_alloc_frame(&m, &p, true, true);
    }
}

static void axiom_frames_full(void)
{
    if (TIER_SANITIZED) {
        /* under -fno-sanitize-recover the bitmap's 1 << 31 aborts: observe it */
        CHECK_KNOWN("F-MM-SHIFT31", !tier_crashes(alloc_64_frames, NULL),
                    "allocating 64 frames trips UBSan (shift of int 1 by 31)");
        return;
    }
    TIER_SKIP_KNOWN("F-MM-SHIFT31", "visible only under -fsanitize=undefined (see the san build)");
    mm_init(&MM);
    static page_t pg;
    uint32_t n = 0;
    for (;;) {
        page_t p = {0};
        p.frame = 0;
        mm_alloc_frame(&MM, &p, true, true);
        if (!p.present) break;
        n++;
        if (n > MAX_PAGES + 1) break;
    }
    CHECK(n == MAX_PAGES && MM.used_pages == MAX_PAGES && mm_get_free_memory(&MM) == 0,
          "frame bitmap at 100%% after %u frames", n);
    pg.frame = 0;
    pg.present = false;
    mm_alloc_frame(&MM, &pg, true, true);
    CHECK(!pg.present && MM.used_pages == MAX_PAGES, "frame MAX+1 refused: page not present");
    page_t last = {0};
    last.frame = MAX_PAGES - 1;
    last.present = true;
    mm_free_frame(&MM, &last);
    page_t again = {0};
    mm_alloc_frame(&MM, &again, true, true);
    CHECK(again.present && again.frame == MAX_PAGES - 1, "the freed last frame is reusable");
}

/* ===== audio rings and the shared pool ===== */
static audio_device_t AD, AD2;

static void axiom_audio(void)
{
    audio_init(&AD, AUDIO_CTRL_HDA, "a");
    uint32_t id = audio_create_stream(&AD, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    CHECK(id != 0, "stream created");
    uint32_t cap = AUDIO_STREAM_BUF_SIZE - 1; /* reserved-slot ring */
    CHECK(audio_stream_space(&AD, id) == (int32_t) cap, "empty ring: space = size-1");
    static uint8_t buf[AUDIO_STREAM_BUF_SIZE * 2];
    for (uint32_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t) (i * 7);
    CHECK(audio_write(&AD, id, buf, 0) == 0, "zero-length write");
    CHECK(audio_write(&AD, id, buf, cap - 1) == (int) (cap - 1), "write MAX-1");
    CHECK(audio_stream_space(&AD, id) == 1, "one byte left");
    CHECK(audio_write(&AD, id, buf, 2) == 1, "short write takes exactly the last byte");
    CHECK(audio_write(&AD, id, buf, 1) == 0 && audio_stream_available(&AD, id) == (int32_t) cap,
          "full ring accepts 0, keeps size-1 queued");
    CHECK(audio_write(&AD, 0, buf, 1) == AUDIO_ENOSTREAM &&
              audio_write(&AD, id + 1, buf, 1) == AUDIO_ENOSTREAM &&
              audio_write(&AD, id, NULL, 1) == AUDIO_EINVAL &&
              audio_write(NULL, id, buf, 1) == AUDIO_EINVAL,
          "write: stream id bounds and NULL");
    uint8_t rd[4];
    CHECK(audio_read(&AD, id, rd, 4) == AUDIO_EDIR, "read on a playback stream refused");
    /* stream table and pool at 100% */
    uint32_t made = 1;
    while (audio_create_stream(&AD, false, AUDIO_FMT_PCM_S16LE, 48000, 2) != 0) made++;
    CHECK(made == AUDIO_MAX_STREAMS && audio_pool_slots_free() == 0,
          "%u streams: table and pool at 100%%", made);
    /* the pool is shared: a second device gets nothing while the first holds it */
    audio_init(&AD2, AUDIO_CTRL_HDA, "b");
    CHECK(audio_create_stream(&AD2, false, AUDIO_FMT_PCM_S16LE, 48000, 2) == 0 &&
              AD2.num_streams == 0,
          "pool exhausted by another device: create fails closed");
    audio_init(&AD, AUDIO_CTRL_HDA, "a"); /* releases AD's slots */
    CHECK(audio_pool_slots_free() == AUDIO_POOL_SLOTS, "re-init releases every slot");
    CHECK(audio_create_stream(&AD2, false, AUDIO_FMT_PCM_S16LE, 48000, 2) != 0,
          "the other device can create again");
    CHECK(audio_create_stream(&AD, false, AUDIO_FMT__COUNT, 48000, 2) == 0 &&
              audio_create_stream(&AD, false, AUDIO_FMT_PCM_S16LE, 0, 2) == 0 &&
              audio_create_stream(&AD, false, AUDIO_FMT_PCM_S16LE, 48000, 0) == 0 &&
              audio_create_stream(NULL, false, AUDIO_FMT_PCM_S16LE, 48000, 2) == 0,
          "create: format/rate/channels bounds and NULL");
    audio_init(&AD, AUDIO_CTRL_HDA, "a");
    audio_init(&AD2, AUDIO_CTRL_HDA, "b");
}

int main(void)
{
    tier_begin("tier1/axioms_alloc", KNOWN_FAILURES, TIER_N(KNOWN_FAILURES));
    axiom_fs_malloc();
    if (map_heap()) {
        axiom_kmalloc();
        axiom_frames();
        axiom_mm_more();
        axiom_frames_full();
    } else {
        CHECK(0, "cannot map the kernel heap page at 0x%x on this host", KERNEL_HEAP_BASE);
    }
    axiom_audio();
    return tier_end();
}
