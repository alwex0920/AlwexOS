#include "include/keyboard.h"
#include "include/lib.h"

#define KEYBOARD_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

static const char keymap[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void keyboard_init() {
    outb(KEYBOARD_STATUS_PORT, 0xAE);
}

int keyboard_getkey() {
    if (!(inb(KEYBOARD_STATUS_PORT) & 0x01)) {
        return -1;
    }
    
    uint8_t scancode = inb(KEYBOARD_PORT);

    if (scancode == 0xE0) {
        while (!(inb(KEYBOARD_STATUS_PORT) & 0x01));
        uint8_t code = inb(KEYBOARD_PORT);
        switch (code) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            default: return -1;
        }
    }

    if (scancode < 0x80) {
        return keymap[scancode];
    }

    return -1;
}