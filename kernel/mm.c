#include "include/mm.h"
#include "include/lib.h"

static free_block_t* free_list = NULL;
static uint32_t heap_start = 0;
static uint32_t heap_end = 0;
static uint32_t heap_size = 0;

static size_t used_memory = 0;
static size_t free_memory = 0;
static size_t allocated_blocks = 0;
static size_t free_blocks = 0;

void mm_init(uint32_t start, uint32_t size) {
    heap_start = start;
    heap_size = size;
    heap_end = heap_start + heap_size;

    free_list = (free_block_t*)heap_start;
    free_list->size = heap_size - sizeof(free_block_t);
    free_list->next = NULL;
    
    free_memory = free_list->size;
    free_blocks = 1;
    used_memory = 0;
    allocated_blocks = 0;
    
    print("Memory manager initialized: ");
    print_hex(heap_start);
    print(" - ");
    print_hex(heap_end);
    print(" (");
    print_hex(heap_size);
    print(" bytes)\n");
}

static void split_block(free_block_t* block, size_t size) {
    if (block->size > size + sizeof(free_block_t) + sizeof(alloc_header_t)) {
        free_block_t* new_block = (free_block_t*)((uint8_t*)block + sizeof(free_block_t) + size + sizeof(alloc_header_t));
        new_block->size = block->size - size - sizeof(alloc_header_t) - sizeof(free_block_t);
        new_block->next = block->next;
        
        block->size = size;
        block->next = new_block;
        
        free_memory -= sizeof(free_block_t);
        free_blocks++;
    }
}

static void coalesce_blocks() {
    free_block_t* current = free_list;
    while (current != NULL && current->next != NULL) {
        if ((uint8_t*)current + sizeof(free_block_t) + current->size == (uint8_t*)current->next) {

            current->size += sizeof(free_block_t) + current->next->size;
            current->next = current->next->next;
            
            free_memory += sizeof(free_block_t);
            free_blocks--;
        } else {
            current = current->next;
        }
    }
}

void* kmalloc(size_t size) {
    if (size == 0 || free_list == NULL) {
        return NULL;
    }

    size = ALIGN_UP(size, 4);

    free_block_t* prev = NULL;
    free_block_t* current = free_list;
    
    while (current != NULL) {
        if (current->size >= size) {
            if (prev != NULL) {
                prev->next = current->next;
            } else {
                free_list = current->next;
            }
            
            alloc_header_t* header = (alloc_header_t*)((uint8_t*)current + sizeof(free_block_t));
            header->size = size;
            header->used = 1;

            split_block(current, size);

            used_memory += size + sizeof(alloc_header_t);
            free_memory -= size + sizeof(alloc_header_t);
            allocated_blocks++;
            free_blocks--;
            
            return (void*)((uint8_t*)header + sizeof(alloc_header_t));
        }
        
        prev = current;
        current = current->next;
    }

    print("kmalloc: out of memory (requested ");
    print_hex(size);
    print(" bytes)\n");
    return NULL;
}

void* kcalloc(size_t num, size_t size) {
    size_t total_size = num * size;
    void* ptr = kmalloc(total_size);
    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

void* krealloc(void* ptr, size_t size) {
    if (ptr == NULL) {
        return kmalloc(size);
    }
    
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    alloc_header_t* header = (alloc_header_t*)((uint8_t*)ptr - sizeof(alloc_header_t));

    if (size <= header->size) {
        return ptr;
    }

    void* new_ptr = kmalloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, ptr, header->size);

    kfree(ptr);
    
    return new_ptr;
}

void kfree(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    alloc_header_t* header = (alloc_header_t*)((uint8_t*)ptr - sizeof(alloc_header_t));
    
    if (!header->used) {
        print("kfree: double free detected\n");
        return;
    }

    header->used = 0;

    free_block_t* block = (free_block_t*)((uint8_t*)header - sizeof(free_block_t));
    block->size = header->size;
    block->next = free_list;
    free_list = block;

    used_memory -= block->size + sizeof(alloc_header_t);
    free_memory += block->size + sizeof(alloc_header_t);
    allocated_blocks--;
    free_blocks++;

    coalesce_blocks();
}

void* kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment < 4) {
        alignment = 4;
    }

    size_t actual_size = size + alignment + sizeof(void*) + sizeof(alloc_header_t);
    void* ptr = kmalloc(actual_size);
    
    if (ptr == NULL) {
        return NULL;
    }

    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned_addr = ALIGN_UP(addr + sizeof(void*) + sizeof(alloc_header_t), alignment);

    *((void**)(aligned_addr - sizeof(void*))) = ptr;
    
    return (void*)aligned_addr;
}

void* kmalloc_dma(size_t size) {
    return kmalloc_aligned(size, 256);
}

void mm_print_stats(void) {
    print("Memory stats: ");
    print_hex(used_memory);
    print(" bytes used, ");
    print_hex(free_memory);
    print(" bytes free, ");
    print_hex(allocated_blocks);
    print(" allocated blocks, ");
    print_hex(free_blocks);
    print(" free blocks\n");
}

size_t mm_get_free_memory(void) {
    return free_memory;
}

size_t mm_get_used_memory(void) {
    return used_memory;
}
