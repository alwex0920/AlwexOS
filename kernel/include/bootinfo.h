#ifndef BOOTINFO_H
#define BOOTINFO_H

#include "stdint.h"

typedef enum {
    BT_UNKNOWN,
    BT_BIOS,
    BT_UEFI
} boot_type_t;

typedef struct {
    void* system_table;
    void* memory_map;
    unsigned long long memory_map_size;
    unsigned long long descriptor_size;
    boot_type_t boot_type;
    uint32_t screen_width;
    uint32_t screen_height;
    uint8_t video_mode;

    void* framebuffer_base;
    unsigned int framebuffer_width;
    unsigned int framebuffer_height;
    unsigned int framebuffer_pixels_per_scanline;
} boot_info_t;

#endif