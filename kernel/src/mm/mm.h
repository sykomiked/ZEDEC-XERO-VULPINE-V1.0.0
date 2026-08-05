/* mm.h — Memory Management: Paging, Heap, Page Frame Allocator
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef MM_H
#define MM_H

#include <stdint.h>
#include <stdbool.h>
#include "../../include/m5_types.h"

#define PAGE_SIZE       4096
#define PAGE_SHIFT      12
#define MAX_PAGES       262144  /* 1GB / 4KB */
#define HEAP_START      0x200000
#define HEAP_MAX        0x4000000  /* 64MB */
#define KERNEL_HEAP_BASE 0x300000

typedef struct page {
    uint32_t frame;
    bool present;
    bool rw;
    bool user;
    bool accessed;
    bool dirty;
    uint32_t ref_count;
} page_t;

typedef struct page_table {
    page_t pages[1024];
} page_table_t;

typedef struct page_directory {
    page_table_t *tables[1024];
    uint32_t tables_phys[1024];
    uint32_t phys_addr;
} page_directory_t;

typedef struct heap_block {
    uint32_t size;
    bool free;
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

typedef struct mm_state {
    page_directory_t *kernel_dir;
    uint32_t frames_bitmap[MAX_PAGES / 32];
    uint32_t total_pages;
    uint32_t used_pages;
    heap_block_t *heap_head;
    uint32_t heap_start;
    uint32_t heap_end;
    uint32_t heap_used;
} mm_state_t;

void mm_init(mm_state_t *mm);
void *kmalloc(mm_state_t *mm, uint32_t size);
void kfree(mm_state_t *mm, void *ptr);
void *kcalloc(mm_state_t *mm, uint32_t count, uint32_t size);
void *krealloc(mm_state_t *mm, void *ptr, uint32_t new_size);

void mm_switch_directory(page_directory_t *dir);
page_t *mm_get_page(mm_state_t *mm, uint32_t addr, page_directory_t *dir, bool make);
void mm_alloc_frame(mm_state_t *mm, page_t *page, bool is_kernel, bool is_writable);
void mm_free_frame(mm_state_t *mm, page_t *page);

uint32_t mm_get_total_memory(void);
uint32_t mm_get_used_memory(mm_state_t *mm);
uint32_t mm_get_free_memory(mm_state_t *mm);
rational_t mm_get_used_rational(void);

#endif
