#include "include/stddef.h"
#include "include/stdint.h"
#include "include/lib.h"
#include "include/keyboard.h"
#include "include/bootinfo.h"

static boot_info_t* g_boot_info = NULL;
static uint32_t* framebuffer = NULL;
static unsigned int fb_width = 0;
static unsigned int fb_height = 0;
static unsigned int fb_pps = 0;
static int fb_x = 0, fb_y = 0;
static uint32_t fb_color = 0x00FFFFFF;
int VGA_WIDTH = 80;
int VGA_HEIGHT = 25;

void clear_screen_uefi();
void update_cursor_position();
void fb_putchar(char c, int x, int y, uint32_t color);
void fb_puts(const char* str, int x, int y, uint32_t color);

inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void msleep(int milliseconds) {
    for (volatile int i = 0; i < milliseconds * 1000; i++) {
        asm volatile ("nop");
    }
}

void sleep(int seconds) {
    msleep(seconds * 1000);
}

void detect_screen_size() {
    if (g_boot_info && g_boot_info->boot_type == BT_UEFI) {
        fb_width = g_boot_info->framebuffer_width;
        fb_height = g_boot_info->framebuffer_height;
    } else {
        uint16_t width, height;
        asm volatile (
            "int $0x10"
            : "=c"(width), "=d"(height)
            : "a"(0x0F00)
            : "memory"
        );
        
        g_boot_info->screen_width = width;
        g_boot_info->screen_height = height;

        if (width == 0 || height == 0) {
            g_boot_info->screen_width = VGA_WIDTH;
            g_boot_info->screen_height = VGA_HEIGHT;
        }
    }
}

#define COM1 0x3F8
#define COM_LSR 5
#define COM_RBR 0

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static int serial_is_transmit_empty() {
    return inb(COM1 + COM_LSR) & 0x20;
}

static void serial_putchar(char c) {
    while (!serial_is_transmit_empty());
    outb(COM1, c);
}

int getchar(void) {
    int c;
    while ((c = keyboard_getkey()) == -1) {
        // Ждем нажатия клавиши
    }
    return c;
}

static uint16_t* const VGA_BUFFER = (uint16_t*)0xB8000;
static uint8_t vga_x = 0, vga_y = 0;
static uint8_t vga_color = 0x0F;

void vga_init(void) {
    detect_screen_size();
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            VGA_BUFFER[y * VGA_WIDTH + x] = ((uint16_t)vga_color << 8) | ' ';
        }
    }
    vga_x = 0;
    vga_y = 0;
}

static void vga_putc(char c) {
    if (c == '\b') {
        if (vga_x > 0) {
            vga_x--;
            VGA_BUFFER[vga_y * VGA_WIDTH + vga_x] = ((uint16_t)vga_color << 8) | ' ';
        } else if (vga_y > 0) {
            vga_y--;
            vga_x = VGA_WIDTH - 1;
            VGA_BUFFER[vga_y * VGA_WIDTH + vga_x] = ((uint16_t)vga_color << 8) | ' ';
        }
        update_cursor_position();
        return;
    }

    if (c == '\r') {
        vga_x = 0;
        update_cursor_position();
        return;
    }

    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else {
        VGA_BUFFER[vga_y * VGA_WIDTH + vga_x] = ((uint16_t)vga_color << 8) | c;
        vga_x++;
    }

    if (vga_x >= VGA_WIDTH) {
        vga_x = 0;
        vga_y++;
    }

    if (vga_y >= VGA_HEIGHT) {
        for (int y = 0; y < VGA_HEIGHT-1; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                VGA_BUFFER[y * VGA_WIDTH + x] = VGA_BUFFER[(y+1) * VGA_WIDTH + x];
            }
        }

        for (int x = 0; x < VGA_WIDTH; x++) {
            VGA_BUFFER[(VGA_HEIGHT-1) * VGA_WIDTH + x] = ((uint16_t)vga_color << 8) | ' ';
        }
        
        vga_y = VGA_HEIGHT - 1;
    }
    
    update_cursor_position();
}

void clear_screen_uefi() {
    if (!framebuffer) return;
    for (unsigned int y = 0; y < fb_height; y++) {
        for (unsigned int x = 0; x < fb_width; x++) {
            framebuffer[y * fb_pps + x] = 0;
        }
    }
    fb_x = 0;
    fb_y = 0;
}

void init_framebuffer(boot_info_t* boot_info) {
    g_boot_info = boot_info;
    
    if (boot_info && boot_info->boot_type == BT_UEFI && boot_info->framebuffer_base) {
        framebuffer = (uint32_t*)boot_info->framebuffer_base;
        fb_width = boot_info->framebuffer_width;
        fb_height = boot_info->framebuffer_height;
        fb_pps = boot_info->framebuffer_pixels_per_scanline;
        
        fb_x = 0;
        fb_y = 0;
        
        clear_screen_uefi();
    } else {
        detect_screen_size();
        fb_width = g_boot_info->screen_width * 8;
        fb_height = g_boot_info->screen_height * 16;
    }
}

void update_cursor_position() {
    if (!g_boot_info || g_boot_info->boot_type == BT_BIOS) {
        uint16_t position = vga_y * VGA_WIDTH + vga_x;
        
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t)(position & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
    }
}

void disable_cursor() {
    if (!g_boot_info || g_boot_info->boot_type == BT_BIOS) {
        outb(0x3D4, 0x0A);
        outb(0x3D5, 0x20);
    }
}

void echo(char c) {
    serial_putchar(c);

    if (!g_boot_info || g_boot_info->boot_type == BT_BIOS) {
        vga_putc(c);
    } else {
        if (c == '\n') {
            fb_x = 0;
            fb_y += 16;
        } else if (c == '\r') {
            fb_x = 0;
        } else if (c == '\b') {
            if (fb_x >= 8) {
                fb_x -= 8;
                fb_putchar(' ', fb_x, fb_y, 0);
            } else if (fb_y > 0) {
                fb_y -= 16;
                fb_x = fb_width - 8;
                fb_putchar(' ', fb_x, fb_y, 0);
            }
        } else {
            fb_putchar(c, fb_x, fb_y, fb_color);
            fb_x += 8;

            if (fb_x >= (int)fb_width - 8) {
                fb_x = 0;
                fb_y += 16;
            }
        }

        vga_x = fb_x / 8;
        vga_y = fb_y / 16;

        if (fb_y >= (int)fb_height - 16) {
            unsigned int scroll_lines = 16;
            for (unsigned int y = scroll_lines; y < fb_height; y++) {
                for (unsigned int x = 0; x < fb_width; x++) {
                    framebuffer[(y - scroll_lines) * fb_pps + x] = framebuffer[y * fb_pps + x];
                }
            }

            for (unsigned int y = fb_height - scroll_lines; y < fb_height; y++) {
                for (unsigned int x = 0; x < fb_width; x++) {
                    framebuffer[y * fb_pps + x] = 0;
                }
            }
            
            fb_y -= scroll_lines;
            vga_x = fb_x / 8;
            vga_y = fb_y / 16;
        }

        if (c == '\b') {
            vga_x = fb_x / 8;
            vga_y = fb_y / 16;
        }
    }
}

static const uint8_t font[128][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 1
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 2
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 3
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 4
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 5
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 6
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 7
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 8
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 9
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 10
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 11
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 12
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 13
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 14
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 15
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 16
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 17
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 18
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 19
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 20
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 21
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 22
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 23
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 24
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 25
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 26
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 27
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 28
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 29
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 30
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 31
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 32 (space)
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // 33 (!)
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 34 (")
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // 35 (#)
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // 36 ($)
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // 37 (%)
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // 38 (&)
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // 39 (')
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // 40 (()
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // 41 ())
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // 42 (*)
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // 43 (+)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // 44 (,)
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // 45 (-)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // 46 (.)
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // 47 (/)
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // 48 (0)
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // 49 (1)
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // 50 (2)
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // 51 (3)
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // 52 (4)
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // 53 (5)
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // 54 (6)
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // 55 (7)
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // 56 (8)
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // 57 (9)
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // 58 (:)
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // 59 (;)
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // 60 (<)
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // 61 (=)
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // 62 (>)
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // 63 (?)
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // 64 (@)
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // 65 (A)
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // 66 (B)
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // 67 (C)
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // 68 (D)
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, // 69 (E)
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, // 70 (F)
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // 71 (G)
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // 72 (H)
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 73 (I)
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // 74 (J)
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // 75 (K)
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, // 76 (L)
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // 77 (M)
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // 78 (N)
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // 79 (O)
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, // 80 (P)
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // 81 (Q)
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // 82 (R)
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // 83 (S)
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 84 (T)
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // 85 (U)
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // 86 (V)
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // 87 (W)
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // 88 (X)
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // 89 (Y)
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // 90 (Z)
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // 91 ([)
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // 92 (\)
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // 93 (])
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // 94 (^)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // 95 (_)
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // 96 (`)
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // 97 (a)
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // 98 (b)
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // 99 (c)
    {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00}, // 100 (d)
    {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00}, // 101 (e)
    {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00}, // 102 (f)
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // 103 (g)
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // 104 (h)
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 105 (i)
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // 106 (j)
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // 107 (k)
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // 108 (l)
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // 109 (m)
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // 110 (n)
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // 111 (o)
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // 112 (p)
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // 113 (q)
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // 114 (r)
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // 115 (s)
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // 116 (t)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // 117 (u)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // 118 (v)
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // 119 (w)
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // 120 (x)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // 121 (y)
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // 122 (z)
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // 123 ({)
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // 124 (|)
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // 125 (})
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 126 (~)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // 127
};

void fb_putchar(char c, int x, int y, uint32_t color) {
    if (!framebuffer || c < 0 || c > 127) return;

    if (c == ' ') {
        for (int dy = 0; dy < 8; dy++) {
            for (int dx = 0; dx < 8; dx++) {
                int px = x + dx;
                int py = y + dy;
                if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                    framebuffer[py * fb_pps + px] = 0;
                }
            }
        }
        return;
    }
    
    const uint8_t* glyph = font[(int)c];
    
    for (int dy = 0; dy < 8; dy++) {
        for (int dx = 0; dx < 8; dx++) {
            if (glyph[dy] & (1 << dx)) {
                int px = x + dx;
                int py = y + dy;
                
                if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                    framebuffer[py * fb_pps + px] = color;
                }
            }
        }
    }
}

void fb_puts(const char* str, int x, int y, uint32_t color) {
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            y += 10;
            cx = x;
        } else {
            fb_putchar(*str, cx, y, color);
            cx += 8;
        }
        str++;
    }
}

void print(const char* str) {
    const char* s = str;
    while (*s) {
        serial_putchar(*s);
        s++;
    }

    while (*str) {
        echo(*str);
        str++;
    }
}

void print_string(const char *str) {
    print(str);
}

void print_hex(uint32_t n) {
    char buf[9];
    const char* hex_chars = "0123456789ABCDEF";
    
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex_chars[n & 0xF];
        n >>= 4;
    }
    buf[8] = '\0';
    print(buf);
}

int snprintf(char *buf, int buf_size, const char *fmt, const char *a, const char *b) {
    int pos = 0;
    for (int i = 0; fmt[i] != '\0' && pos < buf_size - 1; i++) {
        if (fmt[i] == '%' && fmt[i+1] == 's') {
            const char *str = (pos == 0 ? a : b);
            if (!str) str = "(null)";
            for (int j = 0; str[j] && pos < buf_size - 1; j++) {
                buf[pos++] = str[j];
            }
            i++;
        } else {
            buf[pos++] = fmt[i];
        }
    }
    buf[pos] = '\0';
    return pos;
}

int putchar(int c) {
    echo((char)c);
    return c;
}

size_t strlen(const char* str) {
    if (!str) return 0;
    size_t len = 0;
    while (str[len] != '\0') len++;
    return len;
}

char *strcat(char *dest, const char *src) {
    if (!dest || !src) return NULL;
    
    char *d = dest;
    while (*d) d++;
    
    while ((*d++ = *src++));
    
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return -1;
    
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void set_cursor_position(uint8_t x, uint8_t y) {
    vga_x = x;
    vga_y = y;
    if (g_boot_info && g_boot_info->boot_type == BT_UEFI) {
        fb_x = x * 8;
        fb_y = y * 16;
    }
}

void clear_line(int y) {
    uint8_t saved_x = vga_x;
    uint8_t saved_y = vga_y;
    
    set_cursor_position(0, y);
    for (int i = 0; i < VGA_WIDTH; i++) {
        vga_putc(' ');
    }
    
    set_cursor_position(saved_x, saved_y);
}

int strncmp(const char* s1, const char* s2, size_t n) {
    if (!s1 || !s2 || n == 0) return 0;
    
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) 
            return s1[i] - s2[i];
        if (s1[i] == '\0') 
            return 0;
    }
    return 0;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (*needle == '\0') return (char *)haystack;
    
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        
        if (*n == '\0') return (char *)haystack;
    }
    
    return NULL;
}

char *strrchr(const char *s, int c) {
    if (!s) return NULL;
    
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    
    if ((char)c == '\0') return (char *)s;
    return (char *)last;
}

const unsigned short **__ctype_b_loc(void) {
    static const unsigned short ctype_table[256] = {
        [0x00 ... 0x08] = 0x8000,
        [0x09] = 0x2000,
        [0x0A ... 0x0D] = 0x2000,
        [0x0E ... 0x1F] = 0x8000,
        [0x20] = 0x2000,
        [0x21 ... 0x2F] = 0,
        [0x30 ... 0x39] = 0x04,
        [0x3A ... 0x40] = 0,
        [0x41 ... 0x5A] = 0x01,
        [0x5B ... 0x60] = 0,
        [0x61 ... 0x7A] = 0x02,
        [0x7B ... 0x7E] = 0,
        [0x7F] = 0x8000
    };
    
    static const unsigned short *table = ctype_table;
    return &table;
}

char* strchr(const char* s, int c) {
    if (!s) return NULL;
    
    while (*s) {
        if (*s == c) return (char*)s;
        s++;
    }
    return NULL;
}

char* strtok(char* str, const char* delim) {
    static char* saved = NULL;
    if (str) saved = str;
    if (!saved || !*saved) return NULL;
    
    while (*saved && strchr(delim, *saved)) saved++;
    if (!*saved) return NULL;
    
    char* start = saved;
    while (*saved && !strchr(delim, *saved)) saved++;
    
    if (*saved) {
        *saved = '\0';
        saved++;
    }
    
    return start;
}

char* strcpy(char* dest, const char* src) {
    if (!dest || !src) return NULL;
    
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    if (!dest || !src) return NULL;
    
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t i = 0;
    if (size > 0) {
        for (i = 0; i < size - 1 && src[i]; i++)
            dst[i] = src[i];
        dst[i] = 0;
    }
    while (src[i]) i++;
    return i;
}

int safe_readline(char *buf, int size) {
    if (!buf || size <= 0) return 0;
    int i = 0;
    int c;
    
    while (i < size - 1) {
        c = getchar();

        if (c == '\r' || c == '\n') {
            buf[i] = '\0';
            echo('\n');
            return i;
        }

        if (c == 0x08 || c == 0x7F) {
            if (i > 0) {
                i--;
                if (g_boot_info && g_boot_info->boot_type == BT_UEFI) {
                    echo('\b');
                } else {
                    echo('\b');
                    echo(' ');
                    echo('\b');
                }
            }
            continue;
        }

        if (c >= 32 && c <= 126) {
            buf[i++] = (char)c;
            echo((char)c);
        }
    }
    buf[i] = '\0';
    return i;
}

void* memcpy(void* dest, const void* src, size_t n) {
    if (!dest || !src) return NULL;
    
    char* d = dest;
    const char* s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void* memset(void* ptr, int value, size_t num) {
    if (!ptr) return NULL;
    
    unsigned char* p = ptr;
    while (num--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

void* memmove(void* dest, const void* src, size_t n) {
    if (!dest || !src) return NULL;
    
    char* d = dest;
    const char* s = src;
    
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

void clear_screen() {
    if (g_boot_info && g_boot_info->boot_type == BT_UEFI) {
        clear_screen_uefi();
    } else {
        vga_init();
        disable_cursor();
    }
}

void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void poweroff() {
    print("Powering off...\n");
    
    outw(0x604, 0x2000);
    
    outw(0xB004, 0x2000);

    outw(0x4004, 0x3400);

    outb(0xf4, 0x00);

    print("Now the computer power can be turned off via the button\n");

    while (1) asm volatile ("hlt");
}

void reboot() {
    print("System rebooting...\n");

    uint8_t status;
    int attempts = 0;
    do {
        status = inb(0x64);
        if ((status & 0x02) == 0) {
            outb(0x64, 0xFE);
            for (volatile int i = 0; i < 1000000; i++);
            break;
        }
        attempts++;
        if (attempts > 10) break;
    } while (1);

    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);

    print("Now the computer power can be turned off via the button\n");
    
    while (1) asm volatile ("hlt");
}

int atoi(const char* str) {
    int res = 0;
    int sign = 1;
    
    if (*str == '-') {
        sign = -1;
        str++;
    }
    
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    
    return sign * res;
}

double pow(double base, int exp) {
    double result = 1.0;
    for (int i = 0; i < exp; i++) result *= base;
    return result;
}

double fact(int n) {
    double r = 1.0;
    for (int i = 2; i <= n; i++) r *= i;
    return r;
}

double exp(double x) {
    double sum = 1.0;
    double term = 1.0;
    for (int n = 1; n < 20; n++) {
        term *= x / n;
        sum += term;
    }
    return sum;
}

double tanh(double x) {
    double ex = exp(x);
    double enx = exp(-x);
    return (ex - enx) / (ex + enx);
}

size_t strlcat(char *dst, const char *src, size_t size) {
    size_t dst_len = strlen(dst);
    size_t src_len = strlen(src);
    if (dst_len < size) {
        strlcpy(dst + dst_len, src, size - dst_len);
    }
    return dst_len + src_len;
}

char *strncat(char *dest, const char *src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    
    char *d = dest;
    while (*d) d++;

    while (n-- && *src) {
        *d++ = *src++;
    }
    *d = '\0';
    
    return dest;
}

void itoa(int num, char *str, int base) {
    int i = 0;
    int is_negative = 0;

    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (num < 0 && base == 10) {
        is_negative = 1;
        num = -num;
    }

    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem < 10) ? rem + '0' : rem - 10 + 'a';
        num = num / base;
    }

    if (is_negative) {
        str[i++] = '-';
    }

    str[i] = '\0';

    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}
