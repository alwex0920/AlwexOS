#ifndef LIB_H
#define LIB_H

#include "stddef.h"
#include "stdint.h"
#include "bootinfo.h"

int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strchr(const char* s, int c);
char* strtok(char* str, const char* delim);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlen(const char* str);
size_t strlcat(char *dst, const char *src, size_t size);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
void itoa(int num, char *str, int base);
char *strstr(const char *haystack, const char *needle);
char *strrchr(const char *s, int c);
const unsigned short **__ctype_b_loc(void);
int atoi(const char* str);
extern int VGA_WIDTH;
extern int VGA_HEIGHT;
#define CHECK_FLAG(flags, bit) ((flags) & (1 << (bit)))

void init_framebuffer(boot_info_t* boot_info);
void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
void outw(uint16_t port, uint16_t val);
uint16_t inw(uint16_t port);
static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}
void msleep(int milliseconds);
void sleep(int seconds);

void print(const char* str);
void print_string(const char *str);
void print_hex(uint32_t n);
int putchar(int c);
int getchar(void);
void clear_screen(void);

void poweroff(void);
void reboot(void);

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *ptr, int value, size_t num);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
int safe_readline(char *buf, int size);
void set_cursor_position(uint8_t x, uint8_t y);
void clear_line(int y);

void serial_init(void);
void vga_init(void);
void shell_main(void);

#endif
