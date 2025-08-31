#include "include/pci.h"
#include "include/lib.h"

uint32_t pci_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    uint32_t result = inl(0xCFC);

    if (result == 0xFFFFFFFF || result == 0) {
        print("Invalid PCI read: bus=");
        print_hex(bus);
        print(", slot=");
        print_hex(slot);
        print(", func=");
        print_hex(func);
        print(", offset=");
        print_hex(offset);
        print(", result=");
        print_hex(result);
        print("\n");
    }
    
    return result;
}

void pci_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    outl(0xCFC, value);
}
