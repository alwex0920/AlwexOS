#include "include/ahci.h"
#include "include/lib.h"
#include "include/pci.h"
#include "include/mm.h"

#define AHCI_CLASS 0x01
#define AHCI_SUBCLASS 0x06

static hba_mem_t* hba = NULL;
static uint32_t abar = 0;
static int ports[32] = {0};
static int port_count = 0;

static int find_ahci_controller() {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = pci_read_dword(bus, slot, func, 0);
                if (id == 0xFFFFFFFF) continue;
                
                uint32_t class_code = pci_read_dword(bus, slot, func, 0x08);
                uint8_t class = (class_code >> 24) & 0xFF;
                uint8_t subclass = (class_code >> 16) & 0xFF;
                
                if (class == AHCI_CLASS && subclass == AHCI_SUBCLASS) {
                    abar = pci_read_dword(bus, slot, func, 0x24);
                    abar &= ~0xF;

                    uint32_t command = pci_read_dword(bus, slot, func, 0x04);
                    command |= (1 << 2) | (1 << 1);
                    pci_write_dword(bus, slot, func, 0x04, command);
                    
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int check_port_type(hba_port_t* port) {
    uint32_t ssts = port->ssts;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;
    
    if (det != 3 || ipm != 1) return 0;
    
    switch (port->sig) {
        case 0xEB140101:
            return 2;
        case 0x00000101:
            return 1;
        case 0xFFFF0000:
            return 0;
        default:
            return 3;
    }
}

static void port_init(int port_num) {
    hba_port_t* port = &hba->ports[port_num];

    port->cmd &= ~0x01;
    while (port->cmd & 0x4000);
    port->cmd &= ~0x02;

    while (port->cmd & (1 << 15) || port->cmd & (1 << 14));

    void* clb_ptr = kmalloc_aligned(1024, 1024);
    port->clb = (uint32_t)(uintptr_t)clb_ptr;
    port->clbu = (uint32_t)((uint64_t)clb_ptr >> 32);
    memset(clb_ptr, 0, 1024);

    void* fb_ptr = kmalloc_aligned(256, 256);
    port->fb = (uint32_t)(uintptr_t)fb_ptr;
    port->fbu = (uint32_t)((uint64_t)fb_ptr >> 32);
    memset(fb_ptr, 0, 256);

    hba_cmd_header_t* cmd_list = (hba_cmd_header_t*)(uintptr_t)port->clb;
    for (int i = 0; i < 32; i++) {
        void* ctba_ptr = kmalloc_aligned(256, 256);
        cmd_list[i].ctba = (uint32_t)(uintptr_t)ctba_ptr;
        cmd_list[i].ctbau = (uint32_t)((uint64_t)ctba_ptr >> 32);
        memset(ctba_ptr, 0, 256);
    }

    port->cmd |= (1 << 4);

    port->cmd |= 0x01;
}

static int find_cmd_slot(hba_port_t* port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if (!(slots & (1 << i))) {
            return i;
        }
    }
    return -1;
}

static void wait_for_cmd(hba_port_t* port, int slot) {
    while (1) {
        if (!(port->ci & (1 << slot))) {
            break;
        }
        if (port->is & (1 << 30)) {
            print("AHCI: Command error\n");
            break;
        }
    }
    port->is = 0;
}

int ahci_read_sectors(uint64_t lba, uint32_t count, void* buffer) {
    if (port_count == 0) return 0;

    hba_port_t* port = &hba->ports[ports[0]];

    int slot = find_cmd_slot(port);
    if (slot == -1) {
        print("AHCI: No free command slots\n");
        return 0;
    }

    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)(uintptr_t)port->clb + slot;
    hba_cmd_table_t* cmd_table = (hba_cmd_table_t*)(uintptr_t)cmd_header->ctba;

    cmd_header->cfl = sizeof(fis_h2d_t) / sizeof(uint32_t);
    cmd_header->w = 0;
    cmd_header->prdtl = 1;

    fis_h2d_t* fis = (fis_h2d_t*)cmd_table->cfis;
    memset(fis, 0, sizeof(fis_h2d_t));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->command = 0x25;
    fis->device = 0x40;
    fis->lba0 = lba & 0xFF;
    fis->lba1 = (lba >> 8) & 0xFF;
    fis->lba2 = (lba >> 16) & 0xFF;
    fis->lba3 = (lba >> 24) & 0xFF;
    fis->lba4 = (lba >> 32) & 0xFF;
    fis->lba5 = (lba >> 40) & 0xFF;
    fis->count = count & 0xFF;
    fis->count_exp = (count >> 8) & 0xFF;
    fis->control = 0x08;

    cmd_table->prdt[0].dba = (uint32_t)(uintptr_t)buffer;
    cmd_table->prdt[0].dbau = (uint32_t)((uint64_t)(uintptr_t)buffer >> 32);
    cmd_table->prdt[0].dbc = (count * 512) - 1;
    cmd_table->prdt[0].rsv = 0;

    port->ci = 1 << slot;

    wait_for_cmd(port, slot);
    
    return 1;
}

int ahci_write_sectors(uint64_t lba, uint32_t count, void* buffer) {
    if (port_count == 0) return 0;

    hba_port_t* port = &hba->ports[ports[0]];

    int slot = find_cmd_slot(port);
    if (slot == -1) {
        print("AHCI: No free command slots\n");
        return 0;
    }

    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)(uintptr_t)port->clb + slot;
    hba_cmd_table_t* cmd_table = (hba_cmd_table_t*)(uintptr_t)cmd_header->ctba;

    cmd_header->cfl = sizeof(fis_h2d_t) / sizeof(uint32_t);
    cmd_header->w = 1;
    cmd_header->prdtl = 1;

    fis_h2d_t* fis = (fis_h2d_t*)cmd_table->cfis;
    memset(fis, 0, sizeof(fis_h2d_t));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->command = 0x35;
    fis->device = 0x40;
    fis->lba0 = lba & 0xFF;
    fis->lba1 = (lba >> 8) & 0xFF;
    fis->lba2 = (lba >> 16) & 0xFF;
    fis->lba3 = (lba >> 24) & 0xFF;
    fis->lba4 = (lba >> 32) & 0xFF;
    fis->lba5 = (lba >> 40) & 0xFF;
    fis->count = count & 0xFF;
    fis->count_exp = (count >> 8) & 0xFF;
    fis->control = 0x08;

    cmd_table->prdt[0].dba = (uint32_t)(uintptr_t)buffer;
    cmd_table->prdt[0].dbau = (uint32_t)((uint64_t)(uintptr_t)buffer >> 32);
    cmd_table->prdt[0].dbc = (count * 512) - 1;
    cmd_table->prdt[0].rsv = 0;

    port->ci = 1 << slot;

    wait_for_cmd(port, slot);
    
    return 1;
}

void ahci_detect_drives() {
    if (!hba) return;
    
    uint32_t pi = hba->pi;
    port_count = 0;
    
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            int type = check_port_type(&hba->ports[i]);
            if (type == 1) {
                ports[port_count++] = i;
                print("AHCI: Found SATA drive at port ");
                print_hex(i);
                print("\n");

                port_init(i);
            }
        }
    }
}

void ahci_init() {
    print("Initializing AHCI...\n");

    if (!find_ahci_controller()) {
        print("AHCI: Controller not found\n");
        return;
    }
    
    print("AHCI: Controller found at ABAR=");
    print_hex(abar);
    print("\n");

    hba = (hba_mem_t*)(uintptr_t)abar;

    hba->ghc |= (1 << 31);

    while (!(hba->ghc & (1 << 31)));
    
    print("AHCI: Controller enabled\n");

    ahci_detect_drives();
    
    if (port_count > 0) {
        print("AHCI: Initialization completed with ");
        print_hex(port_count);
        print(" drives found\n");
    } else {
        print("AHCI: No drives found\n");
    }
}

uint32_t find_fs_partition() {
    uint8_t buffer[512];

    ahci_read_sectors(0, 1, buffer);

    if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
        print("Found MBR partition table\n");

        for (int i = 0; i < 4; i++) {
            uint8_t* partition_entry = buffer + 446 + i * 16;
            uint8_t partition_type = partition_entry[4];

            if (partition_type != 0) {
                uint32_t lba = *(uint32_t*)(partition_entry + 8);

                uint8_t part_buffer[512];
                ahci_read_sectors(lba, 1, part_buffer);
                
                if (memcmp(part_buffer, "\x4C\x57\x53\x4F\x01\x00\x00\x00", 8) == 0) {
                    print("Found AlwexOS filesystem at LBA: ");
                    print_hex(lba);
                    print("\n");
                    return lba;
                }
            }
        }
    }

    ahci_read_sectors(1, 1, buffer);
    gpt_header_t* header = (gpt_header_t*)buffer;

    if (header->signature == 0x5452415020494645ULL) {
        print("Found GPT partition table\n");

        uint32_t entry_size = header->size_of_partition_entry;
        uint32_t num_entries = header->num_partition_entries;
        uint32_t table_size = entry_size * num_entries;
        uint32_t table_sectors = (table_size + 511) / 512;

        uint8_t* table = kmalloc(table_sectors * 512);
        ahci_read_sectors(header->partition_entries_lba, table_sectors, table);

        for (uint32_t i = 0; i < num_entries; i++) {
            gpt_partition_entry_t* entry = (gpt_partition_entry_t*)(table + i * entry_size);

            if (entry->starting_lba == 0) continue;

            uint8_t part_buffer[512];
            ahci_read_sectors(entry->starting_lba, 1, part_buffer);
            
            if (memcmp(part_buffer, "\x4C\x57\x53\x4F\x01\x00\x00\x00", 8) == 0) {
                print("Found AlwexOS filesystem at LBA: ");
                print_hex(entry->starting_lba);
                print("\n");
                
                kfree(table);
                return entry->starting_lba;
            }
        }
        
        kfree(table);
    }
    
    print("No valid partition table found\n");
    return 0;
}
