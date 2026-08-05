/* mm.c — Memory Management Implementation
 * Page frame allocator with bitmap, kernel heap with linked-list allocator.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "mm.h"
#include "../rmag/rmag_core.h"
#include "../../include/m5_types.h"

static void bitmap_set(uint32_t *bm, uint32_t bit) {
    bm[bit / 32] |= (1 << (bit % 32));
}

static void bitmap_clear(uint32_t *bm, uint32_t bit) {
    bm[bit / 32] &= ~(1 << (bit % 32));
}

static bool bitmap_test(const uint32_t *bm, uint32_t bit) {
    return (bm[bit / 32] & (1 << (bit % 32))) != 0;
}

static int32_t bitmap_first_free(const uint32_t *bm, uint32_t max_bits) {
    for (uint32_t i = 0; i < max_bits / 32; i++) {
        if (bm[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < 32; j++) {
                if (!(bm[i] & (1 << j)))
                    return (int32_t)(i * 32 + j);
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
    mm->heap_head->size = PAGE_SIZE - sizeof(heap_block_t);
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
    bitmap_clear(mm->frames_bitmap, page->frame);
    mm->used_pages--;
    page->frame = 0;
    page->present = false;

    /* M5 RMAG: subtract exact rational quota on free */
    rational_t current = rmag_get_quota((ordinal_t)page->frame);
    rational_t one_page = { (int64_t)PAGE_SIZE, 1 };
    rational_t remaining = rmag_sub_quotas(current, one_page);
    rmag_set_quota((ordinal_t)page->frame, remaining);
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
    if (size == 0) return 0;

    /* Align to 4 bytes */
    size = (size + 3) & ~3;

    heap_block_t *block = mm->heap_head;
    while (block) {
        if (block->free && block->size >= size + sizeof(heap_block_t)) {
            /* Split block */
            if (block->size > size + sizeof(heap_block_t) * 2) {
                heap_block_t *new_block = (heap_block_t *)((uint8_t *)block + sizeof(heap_block_t) + size);
                new_block->size = block->size - size - sizeof(heap_block_t);
                new_block->free = true;
                new_block->next = block->next;
                new_block->prev = block;
                if (block->next) block->next->prev = new_block;
                block->next = new_block;
                block->size = size;
            }
            block->free = false;
            mm->heap_used += block->size + sizeof(heap_block_t);
            return (void *)((uint8_t *)block + sizeof(heap_block_t));
        }
        block = block->next;
    }
    return 0;
}

void kfree(mm_state_t *mm, void *ptr) {
    if (!ptr) return;
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    block->free = true;
    mm->heap_used -= block->size + sizeof(heap_block_t);

    /* Coalesce with next */
    if (block->next && block->next->free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }
    /* Coalesce with prev */
    if (block->prev && block->prev->free) {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

void *kcalloc(mm_state_t *mm, uint32_t count, uint32_t size) {
    void *p = kmalloc(mm, count * size);
    if (p) {
        uint8_t *b = (uint8_t *)p;
        for (uint32_t i = 0; i < count * size; i++) b[i] = 0;
    }
    return p;
}

void *krealloc(mm_state_t *mm, void *ptr, uint32_t new_size) {
    if (!ptr) return kmalloc(mm, new_size);
    if (new_size == 0) { kfree(mm, ptr); return 0; }

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
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
