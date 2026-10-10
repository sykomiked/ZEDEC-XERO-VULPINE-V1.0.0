/* mm.c — Memory Management Implementation
 * Page frame allocator with bitmap, kernel heap with linked-list allocator.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "mm.h"
#include "../rmag/rmag_core.h"
#include "../../include/m5_types.h"

/* Header size as a uint32_t: block sizes are uint32_t and bounded by HEAP_MAX,
 * so heap arithmetic stays in 32 bits instead of narrowing a size_t. */
#define HB_SIZE ((uint32_t) sizeof(heap_block_t))
_Static_assert(sizeof(heap_block_t) % 8 == 0, "heap headers must keep 8-byte payload alignment");

static void bitmap_set(uint32_t *bm, uint32_t bit) {
    bm[bit / 32] |= (1u << (bit % 32));
}

static void bitmap_clear(uint32_t *bm, uint32_t bit) {
    bm[bit / 32] &= ~(1u << (bit % 32));
}

static __attribute__((unused)) bool bitmap_test(const uint32_t *bm, uint32_t bit) {
    return (bm[bit / 32] & (1u << (bit % 32))) != 0;
}

static int32_t bitmap_first_free(const uint32_t *bm, uint32_t max_bits) {
    for (uint32_t i = 0; i < max_bits / 32; i++) {
        if (bm[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < 32; j++) {
                if (!(bm[i] & (1u << j))) return (int32_t) (i * 32 + j);
            }
        }
    }
    return -1;
}

void mm_init(mm_state_t *mm) {
    mm->total_pages = MAX_PAGES;
    mm->used_pages = 0;
    for (uint32_t i = 0; i < MAX_PAGES / 32; i++)
        mm->frames_bitmap[i] = 0;

    mm->heap_start = KERNEL_HEAP_BASE;
    mm->heap_end = KERNEL_HEAP_BASE + PAGE_SIZE;
    mm->heap_used = 0;

    mm->heap_head = (heap_block_t *)(uintptr_t)mm->heap_start;
    mm->heap_head->size = PAGE_SIZE - HB_SIZE;
    mm->heap_head->free = true;
    mm->heap_head->next = 0;
    mm->heap_head->prev = 0;
}

void mm_alloc_frame(mm_state_t *mm, page_t *page, bool is_kernel, bool is_writable) {
    if (page->frame != 0) return;
    int32_t idx = bitmap_first_free(mm->frames_bitmap, mm->total_pages);
    if (idx == -1) return;
    bitmap_set(mm->frames_bitmap, (uint32_t)idx);
    mm->used_pages++;
    page->present = true;
    page->rw = is_writable;
    page->user = !is_kernel;
    page->frame = (uint32_t)idx;

    /* M5 RMAG: record exact rational quota for this frame allocation */
    rational_t current = rmag_get_quota((ordinal_t)idx);
    rational_t one_page = { (int64_t)PAGE_SIZE, 1 };
    rational_t new_quota = rmag_add_quotas(current, one_page);
    rmag_set_quota((ordinal_t)idx, new_quota);
}

void mm_free_frame(mm_state_t *mm, page_t *page) {
    if (page->frame == 0) return;
    uint32_t frame = page->frame; /* read before it is cleared below */
    if (frame >= MAX_PAGES) return;
    bitmap_clear(mm->frames_bitmap, frame);
    mm->used_pages--;
    page->frame = 0;
    page->present = false;

    /* M5 RMAG: subtract exact rational quota on free (from the frame that was
     * freed: this used to read page->frame after zeroing it, so every free
     * debited frame 0's quota instead). */
    rational_t current = rmag_get_quota((ordinal_t) frame);
    rational_t one_page = { (int64_t)PAGE_SIZE, 1 };
    rational_t remaining = rmag_sub_quotas(current, one_page);
    rmag_set_quota((ordinal_t) frame, remaining);
}

page_t *mm_get_page(mm_state_t *mm, uint32_t addr, page_directory_t *dir, bool make) {
    (void)mm;
    addr /= PAGE_SIZE;
    uint32_t table_idx = addr / 1024;
    if (dir->tables[table_idx])
        return &dir->tables[table_idx]->pages[addr % 1024];
    if (make) {
        /* Would allocate a new page table here */
        return 0;
    }
    return 0;
}

void *kmalloc(mm_state_t *mm, uint32_t size) {
    if (size == 0 || size > HEAP_MAX) return 0; /* (size + 7) must not wrap */

    /* Align to 8 bytes. The next block header is carved out right after the
     * payload, and heap_block_t holds pointers: with 4-byte rounding a split
     * on a 64-bit image put that header on a 4-byte boundary (misaligned
     * access, found by UBSan in test_mm_alloc). size <= HEAP_MAX: no wrap. */
    size = (size + 7u) & ~7u;

    heap_block_t *block = mm->heap_head;
    while (block) {
        if (block->free && block->size >= size + HB_SIZE) {
            /* Split block */
            if (block->size > size + HB_SIZE * 2u) {
                heap_block_t *new_block = (heap_block_t *) ((uint8_t *) block + HB_SIZE + size);
                new_block->size = block->size - size - HB_SIZE;
                new_block->free = true;
                new_block->next = block->next;
                new_block->prev = block;
                if (block->next) block->next->prev = new_block;
                block->next = new_block;
                block->size = size;
            }
            block->free = false;
            mm->heap_used += block->size + HB_SIZE;
            return (void *) ((uint8_t *) block + HB_SIZE);
        }
        block = block->next;
    }
    return 0;
}

void kfree(mm_state_t *mm, void *ptr) {
    if (!ptr) return;
    heap_block_t *block = (heap_block_t *) ((uint8_t *) ptr - HB_SIZE);
    if (block->free) return; /* double free: accounting would underflow */
    block->free = true;
    mm->heap_used -= block->size + HB_SIZE;

    /* Coalesce with next */
    if (block->next && block->next->free) {
        block->size += HB_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }
    /* Coalesce with prev */
    if (block->prev && block->prev->free) {
        block->prev->size += HB_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

void *kcalloc(mm_state_t *mm, uint32_t count, uint32_t size) {
    /* count * size wrapping 32 bits used to return a too-small block. */
    uint32_t total;
    if (__builtin_mul_overflow(count, size, &total)) return 0;
    void *p = kmalloc(mm, total);
    if (p) {
        uint8_t *b = (uint8_t *)p;
        for (uint32_t i = 0; i < total; i++) b[i] = 0;
    }
    return p;
}

void *krealloc(mm_state_t *mm, void *ptr, uint32_t new_size) {
    if (!ptr) return kmalloc(mm, new_size);
    if (new_size == 0) { kfree(mm, ptr); return 0; }

    heap_block_t *block = (heap_block_t *) ((uint8_t *) ptr - HB_SIZE);
    if (block->size >= new_size) return ptr;

    void *new_ptr = kmalloc(mm, new_size);
    if (!new_ptr) return 0;

    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (uint32_t i = 0; i < block->size; i++) dst[i] = src[i];
    kfree(mm, ptr);
    return new_ptr;
}

void mm_switch_directory(page_directory_t *dir) {
    (void)dir;
    /* Would load CR3 with dir->phys_addr */
}

uint32_t mm_get_total_memory(void) { return MAX_PAGES * PAGE_SIZE; }
uint32_t mm_get_used_memory(mm_state_t *mm) { return mm->heap_used; }
uint32_t mm_get_free_memory(mm_state_t *mm) { return (MAX_PAGES - mm->used_pages) * PAGE_SIZE; }

/* M5 RMAG: exact rational memory accounting — no float drift */
rational_t mm_get_used_rational(void) {
    rational_t total = { 0, 1 };
    for (uint32_t i = 0; i < MAX_PAGES; i++) {
        rational_t q = rmag_get_quota((ordinal_t)i);
        total = rmag_add_quotas(total, q);
    }
    return total;
}
