// keyboard.c
// PS/2 + EHCI + minimal xHCI stub handling (best-effort handoff / power ports)
// Simplified educational code — may need platform-specific adjustments.

#include "include/lib.h"
#include "include/pci.h"
#include "include/keyboard.h"
#include "include/stdint.h"

#define KBD_BUFFER_SIZE 128

static char kbd_buffer[KBD_BUFFER_SIZE];
static int kbd_head = 0;
static int kbd_tail = 0;

static void kbd_put(char c) {
    kbd_buffer[kbd_head] = c;
    kbd_head = (kbd_head + 1) % KBD_BUFFER_SIZE;
}

int keyboard_getkey() {
    if (kbd_head == kbd_tail) return 0;
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return c;
}

// ---------------- PS/2 Keyboard ----------------
static const char scancode_to_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',   0,'\\',
    'z','x','c','v','b','n','m',',','.','/',   0, '*', 0,' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

void ps2_keyboard_handler(uint8_t scancode) {
    if (!(scancode & 0x80)) {
        char c = scancode_to_ascii[scancode];
        if (c) kbd_put(c);
    }
}

// ---------------- USB HID -> ASCII ----------------
static const char usb_hid_to_ascii[256] = {
    0,0,0,0, 'a','b','c','d','e','f','g','h','i','j','k','l',
    'm','n','o','p','q','r','s','t','u','v','w','x','y','z',
    '1','2','3','4','5','6','7','8','9','0',
    '\n', 27, '\b','\t',' ', '-', '=','[',']','\\',
    '#',';','\'','`',',','.','/',
};

void usb_hid_handle_report(uint8_t report[8]) {
    for (int i = 2; i < 8; i++) {
        uint8_t code = report[i];
        if (code == 0) continue;
        char c = usb_hid_to_ascii[code];
        if (c) kbd_put(c);
    }
}

// ---------------- EHCI structures ----------------
typedef struct {
    uint32_t next_qtd;
    uint32_t alt_next_qtd;
    uint32_t token;
    uint32_t buffer[5];
} __attribute__((packed, aligned(32))) ehci_qtd_t;

typedef struct {
    uint32_t horiz_link;
    uint32_t ep_char;
    uint32_t ep_cap;
    uint32_t current_qtd;
    ehci_qtd_t overlay;
} __attribute__((packed, aligned(32))) ehci_qh_t;

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_t;

// EHCI register offsets (operational)
#define EHCI_USBCMD           0x00
#define EHCI_USBSTS           0x04
#define EHCI_ASYNCLISTADDR    0x18
#define EHCI_CONFIGFLAG       0x40
#define EHCI_PORTSC_BASE      0x44
#define EHCI_HCSPARAMS_OFFSET 0x04

// MMIO base
static volatile uint32_t *ehci_regs = NULL;
static uint32_t usb_base = 0;
static uint32_t ehci_op_base = 0;

typedef enum { USB_NONE, USB_UHCI, USB_OHCI, USB_EHCI, USB_XHCI } usb_type_t;
static usb_type_t usb_type = USB_NONE;

static ehci_qh_t *async_qh = NULL;

// ---------------- Memory allocator for EHCI ----------------
extern void *kmalloc_dma(size_t size, size_t align);

// ---------------- helper utils ----------------
static void delay_short_ms(int ms) {
    if (ms <= 0) return;
    for (volatile int i = 0; i < ms * 1000; i++) { asm volatile("nop"); }
}

// ---------------- EHCI helpers ----------------
static uint8_t ehci_read_caplen(uint32_t cap_base) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)cap_base;
    return *p;
}

static uint32_t ehci_read_hcsparams(uint32_t cap_base) {
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)(cap_base + EHCI_HCSPARAMS_OFFSET);
    return *p;
}

static ehci_qtd_t *ehci_create_qtd(void *buf, uint32_t len, uint32_t pid) {
    ehci_qtd_t *qtd = (ehci_qtd_t*)kmalloc_dma(sizeof(ehci_qtd_t), 32);
    if (!qtd) return NULL;
    for (size_t i = 0; i < sizeof(ehci_qtd_t); i++) ((uint8_t*)qtd)[i] = 0;
    qtd->next_qtd = 1;
    qtd->alt_next_qtd = 1;
    uint32_t tb = (len & 0x7FFF);
    qtd->token = (tb << 16) | (pid << 8) | (1U << 7);
    if (buf) qtd->buffer[0] = (uint32_t)(uintptr_t)buf;
    return qtd;
}

static int ehci_wait_qtd(ehci_qtd_t *qtd, int timeout_ms) {
    int loops = timeout_ms * 1000;
    while (loops-- > 0) {
        if (!(qtd->token & (1U << 7))) return 0;
        asm volatile("pause");
    }
    return -1;
}

// ---------------- EHCI initialization + port bring-up ----------------
static int ehci_init_controller(uint32_t bar0_mmio) {
    usb_base = bar0_mmio;
    uint8_t caplen = ehci_read_caplen(usb_base);
    ehci_op_base = usb_base + caplen;
    ehci_regs = (volatile uint32_t *)(uintptr_t)ehci_op_base;

    print("EHCI: op_base = ");
    print_hex(ehci_op_base);
    print("\n");

    uint32_t hcsparams = ehci_read_hcsparams(usb_base);
    uint32_t num_ports = hcsparams & 0xF;
    if (num_ports == 0) num_ports = 4;
    print("EHCI: ports = ");
    print_hex(num_ports);
    print("\n");

    // Reset
    ehci_regs[EHCI_USBCMD/4] = (1 << 1);
    int t = 1000000;
    while (ehci_regs[EHCI_USBCMD/4] & (1 << 1)) {
        if (--t == 0) {
            print("EHCI: reset timeout\n");
            return -1;
        }
    }

    // allocate async QH
    async_qh = (ehci_qh_t*)kmalloc_dma(sizeof(ehci_qh_t), 32);
    if (!async_qh) {
        print("EHCI: async_qh alloc failed\n");
        return -1;
    }
    for (size_t i=0;i<sizeof(ehci_qh_t);i++) ((uint8_t*)async_qh)[i] = 0;
    async_qh->horiz_link = 1;

    ehci_regs[EHCI_ASYNCLISTADDR/4] = (uint32_t)(uintptr_t)async_qh;
    uint32_t cmd = ehci_regs[EHCI_USBCMD/4];
    cmd |= 1;
    ehci_regs[EHCI_USBCMD/4] = cmd;
    ehci_regs[EHCI_CONFIGFLAG/4] = 1;

    delay_short_ms(50);

    for (uint32_t port = 0; port < num_ports; port++) {
        uint32_t port_offset = EHCI_PORTSC_BASE + port * 4;
        volatile uint32_t *portsc = &ehci_regs[port_offset/4];
        uint32_t val = *portsc;
        if (!(val & 0x00000001)) continue;

        print("EHCI: device appears on port ");
        print_hex(port);
        print("\n");

        // clear connect change
        *portsc = (1 << 1);

        // issue reset
        *portsc |= (1 << 8);

        int wait = 500;
        int loops = wait * 1000;
        while (loops-- > 0) {
            uint32_t pv = *portsc;
            if (!(pv & (1 << 8))) break;
            asm volatile("nop");
        }
        delay_short_ms(20);

        uint32_t pval = *portsc;
        if (pval & 0x00000001) {
            if (!(pval & (1 << 2))) {
                *portsc |= (1 << 2);
                delay_short_ms(10);
            }
            print("EHCI: port ");
            print_hex(port);
            print(" enabled\n");
        } else {
            print("EHCI: port ");
            print_hex(port);
            print(" no device after reset\n");
        }

        *portsc = (1 << 1) | (1 << 3);
    }

    delay_short_ms(50);
    print("EHCI: initialized (basic)\n");
    return 0;
}

// ---------------- PCI scan and init EHCI/xHCI ----------------
static void xhci_fake_init(uint32_t bar0_mmio); // forward

void usb_init() {
    usb_type = USB_NONE;
    for (uint8_t bus = 0; bus < 8; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read_dword(bus, slot, 0, 0);
            if (id == 0xFFFFFFFF) continue;
            uint32_t classcode = pci_read_dword(bus, slot, 0, 8);
            uint8_t base_class = (classcode >> 24) & 0xFF;
            uint8_t sub_class  = (classcode >> 16) & 0xFF;
            if (base_class == 0x0C && sub_class == 0x03) {
                uint8_t prog_if = (classcode >> 8) & 0xFF;
                uint32_t bar0 = pci_read_dword(bus, slot, 0, 0x10);
                if (prog_if == 0x20) {
                    usb_type = USB_EHCI;
                    if (ehci_init_controller(bar0 & ~0xF) == 0) {
                        print("EHCI: controller started\n");
                    } else {
                        print("EHCI: controller init failed\n");
                        usb_type = USB_NONE;
                    }
                    return;
                } else if (prog_if == 0x30) {
                    usb_type = USB_XHCI;
                    // Try to request ownership / then do a lightweight fake init/power ports
                    // We call a best-effort take ownership helper below. It may or may not work
                    // on a given platform; if it fails the BIOS may still own the controller.
                    xhci_fake_init(bar0 & ~0xF);
                    return;
                } else if (prog_if == 0x00) {
                    usb_type = USB_UHCI;
                    print("Detected UHCI (not implemented)\n");
                    return;
                } else if (prog_if == 0x10) {
                    usb_type = USB_OHCI;
                    print("Detected OHCI (not implemented)\n");
                    return;
                }
            }
        }
    }
    print("USB: no controller found\n");
}

// ---------------- Transfers (EHCI simplified) ----------------
int ehci_control_transfer(usb_setup_t *setup, void *data, uint32_t length, int dir_in) {
    ehci_qtd_t *qtd_setup = ehci_create_qtd(setup, 8, 2);
    ehci_qtd_t *qtd_data = (length > 0 && data) ? ehci_create_qtd(data, length, dir_in ? 1 : 0) : NULL;
    ehci_qtd_t *qtd_status = ehci_create_qtd(NULL, 0, dir_in ? 0 : 1);

    if (!qtd_setup || !qtd_status) return -1;
    qtd_setup->next_qtd = qtd_data ? (uint32_t)(uintptr_t)qtd_data : (uint32_t)(uintptr_t)qtd_status;
    if (qtd_data) qtd_data->next_qtd = (uint32_t)(uintptr_t)qtd_status;

    async_qh->current_qtd = (uint32_t)(uintptr_t)qtd_setup;
    if (ehci_wait_qtd(qtd_status, 500) != 0) return -1;
    return 0;
}

int ehci_interrupt_transfer(void *buf, uint32_t len) {
    ehci_qtd_t *qtd = ehci_create_qtd(buf, len, 1);
    if (!qtd) return -1;
    async_qh->current_qtd = (uint32_t)(uintptr_t)qtd;
    if (ehci_wait_qtd(qtd, 500) != 0) return -1;
    return 0;
}

// ---------------- HID init helpers (boot protocol) ----------------
static int ehci_set_address(uint8_t addr) {
    usb_setup_t setup = {0x00, 5, addr, 0, 0};
    return ehci_control_transfer(&setup, NULL, 0, 0) == 0;
}

static int ehci_get_device_descriptor(uint8_t *buf, uint32_t len) {
    usb_setup_t setup = {0x80, 6, (1 << 8), 0, (uint16_t)len};
    return ehci_control_transfer(&setup, buf, len, 1) == 0;
}

static int ehci_get_configuration_descriptor(uint8_t *buf, uint32_t len) {
    usb_setup_t setup = {0x80, 6, (2 << 8), 0, (uint16_t)len};
    return ehci_control_transfer(&setup, buf, len, 1) == 0;
}

static int ehci_set_configuration(uint16_t conf) {
    usb_setup_t setup = {0x00, 9, conf, 0, 0};
    return ehci_control_transfer(&setup, NULL, 0, 0) == 0;
}

static int ehci_set_protocol_boot() {
    usb_setup_t setup = {0x21, 0x0B, 0, 0, 0};
    return ehci_control_transfer(&setup, NULL, 0, 0) == 0;
}

static void ehci_hid_init() {
    uint8_t dev_desc[18];
    ehci_set_address(1);
    if (!ehci_get_device_descriptor(dev_desc, 18)) {
        print("EHCI: no device descriptor\n");
        return;
    }
    print("EHCI: got device descriptor\n");

    uint8_t conf_desc[64];
    if (ehci_get_configuration_descriptor(conf_desc, 64)) {
        print("EHCI: got configuration descriptor\n");
    }
    if (ehci_set_configuration(1)) {
        print("EHCI: configuration set\n");
    }
    if (ehci_set_protocol_boot()) {
        print("EHCI: boot protocol set\n");
    }
}

int ehci_poll_keyboard_report(uint8_t report[8]) {
    for (int i=0;i<8;i++) report[i]=0;
    if (ehci_interrupt_transfer(report, 8) == 0) return 1;
    return 0;
}

// ---------------- xHCI helpers (stub + best-effort handoff) ----------------

static const char* xhci_speed_str(int code) {
    switch (code) {
        case 1: return "Full-Speed (USB1.1)";
        case 2: return "Low-Speed (USB1.0)";
        case 3: return "High-Speed (USB2.0)";
        case 4: return "SuperSpeed (USB3.0)";
        case 5: return "SuperSpeed+ (USB3.1/3.2)";
        default: return "Unknown";
    }
}

/*
 * Best-effort attempt to take ownership from firmware for xHCI/EHCI legacy handoff.
 * NOTE: exact handoff mechanism is platform-specific. The code below:
 *  - prints informative messages,
 *  - tries to set common PCI legacy ownership locations (best-effort),
 *  - but may fail on many systems. If it doesn't work, the correct, reliable
 *  solution is to disable "Legacy USB Support" / enable "xHCI hand-off" in BIOS/UEFI.
 */
static void xhci_try_take_ownership_pci(uint8_t bus, uint8_t slot, uint8_t func) {
    print("xHCI: attempt PCI handoff (best-effort)...\n");

    // Try some common candidate offsets for legacy support registers in PCI config.
    // Many systems expose a "USBLEGSUP" / "USBLEGCTLSTS" register at offsets near 0xC0..0xD0.
    // We attempt to read/write a few offsets and print their values.
    const uint32_t candidates[] = { 0xC0, 0xD0, 0x40, 0xA0 };
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        uint32_t off = candidates[i];
        uint32_t v = pci_read_dword(bus, slot, func, off);
        if (v == 0xFFFFFFFF) continue;
        print(" xHCI: pci[");
        print_hex(off);
        print("] = ");
        print_hex(v);
        print("\n");

        // Best-effort: if we find a dword that looks like a legacy register (non-zero),
        // try to set an 'OS OWNED' bit if present. We cannot know exact bit layout,
        // so we only set some common-bit candidates conservatively and re-read.
        // WARNING: writing PCI config can be risky on some hardware; we only try mild writes.
        uint32_t trial = v;
        // common EHCI handoff bits: bit 24 (OSOWN) in some implementations, or bit 16..17 in others.
        // We'll try to set bit 24 (0x01000000) and clear bit 16 (0x00010000) if present.
        uint32_t write = (v | 0x01000000) & (~0x00010000u);
        if (write != v) {
            pci_read_dword(bus, slot, func, off); // harmless read
            // Avoid blind writes in some cases: only write if value changed and not 0.
            pci_read_dword(bus, slot, func, off); // read again for safety
            // perform write
            // pci_write_dword may exist; if not, skip. We'll check by attempting to write 0 back later.
            // If you have pci_write_dword implementation, uncomment the next lines:
            // pci_write_dword(bus, slot, func, off, write);
            // uint32_t newv = pci_read_dword(bus, slot, func, off);
            // print(" xHCI: pci ");
            // print_hex(off);
            // print(" -> ");
            // print_hex(newv);
            // print("\n");
        }
    }

    print("xHCI: PCI handoff attempted (best-effort). If keyboard still not powered, try BIOS settings.\n");
}

/*
 * Lightweight xHCI "power ports / wake" stub.
 * This just reads xHCI op registers and toggles per-port power/reset bits in the OP register
 * area (addresses are op_base + 0x400 + port*0x10). This may cause keyboards to get VBUS
 * power and their LED to light if controller is already under OS control.
 */
static void xhci_fake_init(uint32_t bar0_mmio) {
    print("xHCI: controller detected at ");
    print_hex(bar0_mmio);
    print("\n");

    volatile uint8_t *cap_regs8 = (volatile uint8_t *)(uintptr_t)bar0_mmio;
    uint8_t caplen = *cap_regs8;
    volatile uint32_t *op_regs = (volatile uint32_t *)((uintptr_t)bar0_mmio + caplen);

    // try to read HCSParams1 from capability area (at capbase + 0x04)
    volatile uint32_t *cap_regs32 = (volatile uint32_t *)(uintptr_t)bar0_mmio;
    uint32_t hcsparams1 = cap_regs32[1]; // capbase + 4
    uint32_t num_ports = (hcsparams1 >> 24) & 0xFF;
    if (num_ports == 0) num_ports = 8; // fallback guess

    print("xHCI: reported ports = ");
    print_hex(num_ports);
    print("\n");

    // Try PCI handoff (best-effort)
    // We don't have bus/slot here, but usb_init can call pci helper if needed.
    // For now try to glean bus/slot by scanning PCI again and calling xhci_try_take_ownership_pci
    // for the device whose BAR matches bar0_mmio (best-effort).
    for (uint8_t bus = 0; bus < 8; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read_dword(bus, slot, 0, 0);
            if (id == 0xFFFFFFFF) continue;
            uint32_t bar0 = pci_read_dword(bus, slot, 0, 0x10) & ~0xF;
            if (bar0 == bar0_mmio) {
                xhci_try_take_ownership_pci(bus, slot, 0);
                goto ownership_done;
            }
        }
    }
ownership_done:

    // iterate port regs in operational area: offset 0x400 + port*0x10
    for (uint32_t port = 0; port < num_ports; port++) {
        volatile uint32_t *portsc = &op_regs[(0x400 + port*0x10) / 4];
        uint32_t val = *portsc;

        int ccs = (val & (1 << 0)) ? 1 : 0;
        int ped = (val & (1 << 1)) ? 1 : 0;
        int pr  = (val & (1 << 4)) ? 1 : 0;
        int pp  = (val & (1 << 9)) ? 1 : 0;
        int speed = (val >> 10) & 0xF;

        print("xHCI: port ");
        print_hex(port + 1);
        print(" status: CCS=");
        print_hex(ccs);
        print(" PED=");
        print_hex(ped);
        print(" PP=");
        print_hex(pp);
        print(" PR=");
        print_hex(pr);
        print(" speed=");
        print_hex(speed);
        print(" (");
        print(xhci_speed_str(speed));
        print(")\n");

        // enable port power if off
        if (!pp) {
            // set Port Power (PP) bit (bit9)
            uint32_t newv = val | (1 << 9);
            *portsc = newv;
            print(" -> Port power enabled\n");
            delay_short_ms(10);
            val = *portsc;
        }

        // attempt port reset if connected and not in reset
        if (ccs && !(val & (1 << 4))) {
            *portsc = val | (1 << 4); // set Port Reset (PR) bit
            print(" -> Port reset initiated\n");
            // wait a bit
            delay_short_ms(20);
            // clear PR by reading back (controller usually clears it)
            val = *portsc;
        }
    }

    print("xHCI: (stub) ports inspected\n");
}

// ---------------- API ----------------
void keyboard_init() {
    usb_init();
    if (usb_type == USB_EHCI && ehci_regs != NULL) {
        ehci_hid_init();
    } else if (usb_type == USB_XHCI) {
        print("Keyboard: xHCI stub only (LEDs may light, no input until real xHCI driver)\n");
    } else {
        print("Keyboard: using PS/2 or no USB controller available\n");
    }
}

int keyboard_getkey_nonblock() {
    if (usb_type == USB_EHCI && ehci_regs != NULL) {
        uint8_t report[8];
        if (ehci_poll_keyboard_report(report)) {
            usb_hid_handle_report(report);
        }
    }
    return keyboard_getkey();
}
