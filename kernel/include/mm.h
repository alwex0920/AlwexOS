#ifndef MM_H
#define MM_H

#include "stddef.h"
#include "stdint.h"

typedef struct free_block {
    size_t size;
    struct free_block* next;
} free_block_t;

typedef struct {
    size_t size;
    uint8_t used;
} alloc_header_t;

void mm_init(uint32_t start, uint32_t size);

void* kmalloc(size_t size);
void* kcalloc(size_t num, size_t size);
void* krealloc(void* ptr, size_t size);
void kfree(void* ptr);

void* kmalloc_aligned(size_t size, size_t alignment);
void* kmalloc_dma(size_t size);

void mm_print_stats(void);
size_t mm_get_free_memory(void);
size_t mm_get_used_memory(void);

#define ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))

#endif
