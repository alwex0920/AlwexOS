#include "include/ahci.h"
#include "include/lib.h"
#include "include/pci.h"
#include "include/mm.h"

#define AHCI_CLASS 0x01
#define AHCI_SUBCLASS 0x06
#define AHCI_DEBUG 1

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
                
                uint16_t vendor = id & 0xFFFF;
                uint16_t device = (id >> 16) & 0xFFFF;
                
                uint32_t class_code = pci_read_dword(bus, slot, func, 0x08);
                uint8_t class = (class_code >> 24) & 0xFF;
                uint8_t subclass = (class_code >> 16) & 0xFF;
                uint8_t prog_if = (class_code >> 8) & 0xFF;
                
                print("PCI: ");
                print_hex(bus); print(":");
                print_hex(slot); print(":");
                print_hex(func); print(" - Vendor:");
                print_hex(vendor); print(" Device:");
                print_hex(device); print(" Class:");
                print_hex(class); print(" Subclass:");
                print_hex(subclass); print(" ProgIF:");
                print_hex(prog_if); print("\n");

                if (class == AHCI_CLASS && subclass == AHCI_SUBCLASS) {
                    print("AHCI controller found at ");
                    print_hex(bus); print(":");
                    print_hex(slot); print(":");
                    print_hex(func); print("\n");

                    abar = pci_read_dword(bus, slot, func, 0x24);
                    abar &= ~0xF;

                    print("ABAR: ");
                    print_hex(abar);
                    print("\n");

                    uint32_t command = pci_read_dword(bus, slot, func, 0x04);
                    command |= (1 << 2) | (1 << 1);
                    pci_write_dword(bus, slot, func, 0x04, command);

                    if (abar == 0 || abar == 0xFFFFFFFF) {
                        print("Error: Invalid ABAR\n");
                        continue;
                    }
                    
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
    
    #if AHCI_DEBUG
    print("Port SSTS: ");
    print_hex(ssts);
    print(" (DET=");
    print_hex(det);
    print(", IPM=");
    print_hex(ipm);
    print(")\n");
    #endif
    
    if (det != 3 || ipm != 1) {
        #if AHCI_DEBUG
        if (det != 0) {
            print("Port not ready: DET=");
            print_hex(det);
            print(", IPM=");
            print_hex(ipm);
            print("\n");
        }
        #endif
        return 0;
    }
    
    #if AHCI_DEBUG
    print("Port signature: ");
    print_hex(port->sig);
    print("\n");
    #endif
    
    switch (port->sig) {
        case 0xEB140101:
            return 2; // SATAPI
        case 0x00000101:
            return 1; // SATA
        case 0xFFFF0000:
            return 0; // No device
        default:
            print("Unknown port signature: ");
            print_hex(port->sig);
            print("\n");
            return 3;
    }
}

static void port_reset(int port_num) {
    hba_port_t* port = &hba->ports[port_num];
    
    print("Resetting port ");
    print_hex(port_num);
    print("\n");

    port->cmd &= ~0x01;
    int timeout = 1000000;
    while ((port->cmd & 0x4000) && timeout-- > 0) {
        io_wait();
    }
    if (timeout <= 0) {
        print("Warning: Port stop timeout\n");
    }

    port->ie = 0;

    port->is = 0xFFFFFFFF;

    uint32_t sctl = port->sctl;
    port->sctl = (sctl & ~0xF) | 0x01;
    msleep(1000);
    
    port->sctl = sctl & ~0xF;
    msleep(1000);

    timeout = 5000000;
    while (timeout-- > 0) {
        uint32_t ssts = port->ssts;
        uint8_t det = ssts & 0xF;
        uint8_t ipm = (ssts >> 8) & 0xF;
        
        if (det == 3 && ipm == 1) {
            print("Port ");
            print_hex(port_num);
            print(" ready: DET=3, IPM=1\n");
            return;
        }
        io_wait();
    }
    
    print("Port reset timeout: SSTS=");
    print_hex(port->ssts);
    print(" (DET=");
    print_hex(port->ssts & 0xF);
    print(", IPM=");
    print_hex((port->ssts >> 8) & 0xF);
    print(")\n");
}

static void port_init(int port_num) {
    hba_port_t* port = &hba->ports[port_num];

    port->cmd &= ~0x01;
    while (port->cmd & 0x4000);
    port->cmd &= ~0x02;

    while (port->cmd & (1 << 15) || port->cmd & (1 << 14));

    void* clb_ptr = kmalloc_aligned(1024, 1024);
    port->clb = (uint64_t)(uintptr_t)clb_ptr;
    port->clbu = (uint64_t)(uintptr_t)clb_ptr >> 32;
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

static int is_m2_port(int port_num) {
    hba_port_t* port = &hba->ports[port_num];

    uint32_t sstatus = port->ssts;
    uint8_t det = sstatus & 0xF;
    uint8_t ipm = (sstatus >> 8) & 0xF;
    uint8_t spd = (sstatus >> 4) & 0xF;
    
    print("Checking port ");
    print_hex(port_num);
    print(" for M.2 characteristics:\n");
    print("  SStatus: ");
    print_hex(sstatus);
    print(" (DET=");
    print_hex(det);
    print(", IPM=");
    print_hex(ipm);
    print(", SPD=");
    print_hex(spd);
    print(")\n");

    if (spd >= 3) {
        print("  High speed detected (SPD=");
        print_hex(spd);
        print("), likely M.2\n");
        return 0;
    }

    print("  Signature: ");
    print_hex(port->sig);
    print("\n");

    if (port->sig == 0xEB140101 || port->sig == 0x00000101) {
        print("  Known M.2 signature detected\n");
        return 0;
    }

    uint32_t sctl = port->sctl;
    print("  SControl: ");
    print_hex(sctl);
    print("\n");

    if ((sctl & 0xF) == 0x1) {
        print("  M.2-like SControl configuration detected\n");
        return 0;
    }

    if (port_num >= 4 && port_num <= 6) {
        print("  Port number in typical M.2 range (4-6)\n");
        return 0;
    }
    
    print("  Port does not appear to be M.2\n");
    return 1;
}

static void init_m2_port(int port_num) {
    hba_port_t* port = &hba->ports[port_num];
    
    print("Initializing M.2 port ");
    print_hex(port_num);
    print("\n");

    port->cmd &= ~0x01;  // Clear FRE
    int timeout = 1000000;
    while ((port->cmd & 0x4000) && timeout-- > 0) {
        io_wait();
    }

    port->ie = 0;
    port->is = 0xFFFFFFFF;

    uint32_t sctl = port->sctl;
    port->sctl = (sctl & ~0xF) | 0x01;
    msleep(5000);
    
    port->sctl = sctl & ~0xF;
    msleep(5000);
    
    port->cmd |= 0x01;

    timeout = 10000000;
    while (timeout-- > 0) {
        uint32_t ssts = port->ssts;
        uint8_t det = ssts & 0xF;
        uint8_t ipm = (ssts >> 8) & 0xF;
        
        if (det == 3 && ipm == 1) {
            print("M.2 port ");
            print_hex(port_num);
            print(" ready\n");
            return;
        }
        io_wait();
    }
    
    print("M.2 port initialization timeout\n");
}

void check_controller_capabilities() {
    if (!hba) return;
    
    print("Controller capabilities:\n");
    print("CAP: ");
    print_hex(hba->cap);
    print("\n");

    uint32_t cap = hba->cap;
    print("64-bit addressing: ");
    print((cap & (1 << 31)) ? "Yes\n" : "No\n");
    
    print("Native command queuing: ");
    print((cap & (1 << 30)) ? "Yes\n" : "No\n");
    
    print("Staggered spin-up: ");
    print((cap & (1 << 27)) ? "Yes\n" : "No\n");
    
    print("Mechanical presence switch: ");
    print((cap & (1 << 26)) ? "Yes\n" : "No\n");
    
    print("Aggressive link power management: ");
    print((cap & (1 << 25)) ? "Yes\n" : "No\n");
    
    print("Activity LED: ");
    print((cap & (1 << 24)) ? "Yes\n" : "No\n");
    
    print("Command list override: ");
    print((cap & (1 << 23)) ? "Yes\n" : "No\n");

    if (hba->cap2) {
        print("CAP2: ");
        print_hex(hba->cap2);
        print("\n");
        
        print("BOH: ");
        print((hba->cap2 & (1 << 0)) ? "Yes\n" : "No\n");
        print("NVMe: ");
        print((hba->cap2 & (1 << 1)) ? "Yes\n" : "No\n");
        print("APST: ");
        print((hba->cap2 & (1 << 2)) ? "Yes\n" : "No\n");
    }
}

void diagnose_m2_port(int port_num) {
    hba_port_t* port = &hba->ports[port_num];
    
    print("=== M.2 Port ");
    print_hex(port_num);
    print(" Diagnosis ===\n");
    
    print("SSTS: ");
    print_hex(port->ssts);
    print(" (DET=");
    print_hex(port->ssts & 0xF);
    print(", IPM=");
    print_hex((port->ssts >> 8) & 0xF);
    print(")\n");
    
    print("SCTL: ");
    print_hex(port->sctl);
    print("\n");
    
    print("SERR: ");
    print_hex(port->serr);
    print("\n");
    
    print("SACT: ");
    print_hex(port->sact);
    print("\n");
    
    print("CI: ");
    print_hex(port->ci);
    print("\n");
    
    print("TFD: ");
    print_hex(port->tfd);
    print("\n");
    
    print("SIG: ");
    print_hex(port->sig);
    print("\n");

    uint32_t sstatus = port->ssts;
    print("SStatus: ");
    print_hex(sstatus);
    print(" (DET=");
    print_hex(sstatus & 0xF);
    print(", IPM=");
    print_hex((sstatus >> 8) & 0xF);
    print(", SPD=");
    print_hex((sstatus >> 4) & 0xF);
    print(")\n");
    
    if (port->serr != 0) {
        print("Error detected: ");
        print_hex(port->serr);
        print("\n");
    }
}

int is_port_ready(int port_num) {
    hba_port_t* port = &hba->ports[ports[port_num]];

    uint32_t ssts = port->ssts;
    uint8_t det = ssts & 0xF;
    uint8_t ipm = (ssts >> 8) & 0xF;
    
    if (det != 3 || ipm != 1) {
        print("Port not ready: DET=");
        print_hex(det);
        print(", IPM=");
        print_hex(ipm);
        print("\n");
        return 1;
    }

    if (!(port->cmd & 0x01)) {
        print("Command engine not running\n");
        return 1;
    }

    if (port->tfd & 0x01) {
        print("Port has error: TFD=");
        print_hex(port->tfd);
        print(", SERR=");
        print_hex(port->serr);
        print("\n");
        return 1;
    }
    
    return 0;
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

void test_disk_read() {
    if (port_count == 0) {
        print("No drives available for testing\n");
        return;
    }
    
    print("Testing disk read...\n");

    uint8_t* buffer = (uint8_t*)kmalloc(512);
    if (!buffer) {
        print("Failed to allocate memory for test\n");
        return;
    }

    if (ahci_read_sectors(0, 1, buffer)) {
        print("Disk read test successful\n");

        if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
            print("MBR signature found\n");
        } else {
            print("No MBR signature found\n");
        }
    } else {
        print("Disk read test failed\n");
    }
    
    kfree(buffer);
}

int ahci_read_sectors(uint64_t lba, uint32_t count, void* buffer) {
    if (port_count == 0) {
        print("No ports available\n");
        return 1;
    }

    if (!is_port_ready(0)) {
        print("Port not ready for reading\n");
        return 1;
    }

    hba_port_t* port = &hba->ports[ports[0]];

    int timeout = 1000000;
    while ((port->tfd & 0x88) && timeout-- > 0) {
        io_wait();
    }
    
    if (timeout <= 0) {
        print("AHCI: Port busy timeout\n");
        return 1;
    }

    int slot = find_cmd_slot(port);
    if (slot == -1) {
        print("AHCI: No free command slots\n");
        return 1;
    }

    hba_cmd_header_t* cmd_header = ((hba_cmd_header_t*)(uintptr_t)port->clb) + slot;
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
    
    return 0;
}

int ahci_write_sectors(uint64_t lba, uint32_t count, void* buffer) {
    if (port_count == 0) return 1;

    hba_port_t* port = &hba->ports[ports[0]];

    int slot = find_cmd_slot(port);
    if (slot == -1) {
        print("AHCI: No free command slots\n");
        return 1;
    }

    hba_cmd_header_t* cmd_header = ((hba_cmd_header_t*)(uintptr_t)port->clb) + slot;
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
    
    return 0;
}

void ahci_detect_drives() {
    if (!hba) return;
    
    uint32_t pi = hba->pi;
    port_count = 0;
    
    print("Checking implemented ports: ");
    print_hex(pi);
    print("\n");

    for (int i = 0; i < 32; i++) {
        if ((pi & (1 << i)) && is_m2_port(i)) {
            print("\nM.2 drive port found!");
            print("\n=== Checking M.2 port ");
            print_hex(i);
            print(" ===\n");
            
            diagnose_m2_port(i);

            init_m2_port(i);

            uint32_t ssts = hba->ports[i].ssts;
            uint8_t det = ssts & 0xF;
            uint8_t ipm = (ssts >> 8) & 0xF;
            
            if (det == 3 && ipm == 1) {
                ports[port_count++] = i;
                print("M.2 drive found at port ");
                print_hex(i);
                print("\n");
                port_init(i);
            } else {
                print("No M.2 drive found at port ");
                print_hex(i);
                print("\n");
            }
        }
    }

    for (int i = 0; i < 32; i++) {
        if ((pi & (1 << i)) && !is_m2_port(i)) {
            print("\nM.2 drive port not found!");
            print("\n=== Checking SATA port ");
            print_hex(i);
            print(" ===\n");
            
            hba_port_t* port = &hba->ports[i];
            
            port_reset(i);
            
            int type = check_port_type(port);
            if (type > 0) {
                ports[port_count++] = i;
                print("SATA drive found at port ");
                print_hex(i);
                print(" type: ");
                if (type == 1) print("SATA");
                else if (type == 2) print("SATAPI");
                else print("Unknown");
                print("\n");

                port_init(i);
            } else {
                print("No SATA drive found at port ");
                print_hex(i);
                print("\n");
            }
        }
    }
}

int try_alternative_port_init(int port_num) {
    hba_port_t* port = &hba->ports[port_num];
    
    print("Trying alternative port initialization for port ");
    print_hex(port_num);
    print("\n");

    port->cmd &= ~0x01;
    port->cmd &= ~0x02;
    while (port->cmd & 0x4000);

    port->serr = 0xFFFFFFFF;

    msleep(5000);

    port->cmd |= 0x01;

    int timeout = 1000000;
    while (timeout-- > 0) {
        if (port->ssts != 0) {
            print("Port became responsive after alternative init\n");
            return 0;
        }
        io_wait();
    }
    return 1;
}

int ahci_check_drive_ready(int port_num) {
    if (port_count == 0 || port_num >= port_count) return 0;
    
    hba_port_t* port = &hba->ports[ports[port_num]];
    uint32_t ssts = port->ssts;
    uint8_t det = ssts & 0xF;
    uint8_t ipm = (ssts >> 8) & 0xF;
    
    return (det == 3 && ipm == 1);
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
    print("AHCI Controller Details:\n");

    uint32_t cap = hba->cap;

    print("Supports 64-bit addressing: "); print((cap & (1 << 31)) ? "Yes\n" : "No\n");
    print("Number of command slots: "); print_hex(((cap >> 8) & 0x1F) + 1); print("\n");
    print("Supports native command queuing: "); print((cap & (1 << 30)) ? "Yes\n" : "No\n");
    print("Supports staggered spin-up: "); print((cap & (1 << 27)) ? "Yes\n" : "No\n");
    
    hba = (hba_mem_t*)(uintptr_t)abar;

    if (hba->cap == 0 || hba->cap == 0xFFFFFFFF) {
        print("Error: Cannot read AHCI registers\n");
        return;
    }

    uint32_t test_value = 0x12345678;
    hba->ghc = test_value;
    
    if (hba->ghc != test_value) {
        print("Error: Cannot write to AHCI registers\n");
        print("Expected: ");
        print_hex(test_value);
        print(", Got: ");
        print_hex(hba->ghc);
        print("\n");
        return;
    }

    print("CAP: ");
    print_hex(hba->cap);
    print("\n");

    hba->ghc |= (1 << 31);

    int timeout = 5000000;
    while (!(hba->ghc & (1 << 31)) && timeout-- > 0) {
        io_wait();
    }
    
    if (timeout <= 0) {
        print("AHCI: Failed to enable controller\n");
        print("GHC: ");
        print_hex(hba->ghc);
        print("\n");
        return;
    }
    
    print("AHCI: Controller enabled\n");
    print("GHC: ");
    print_hex(hba->ghc);
    print("\n");

    print("Ports implemented: ");
    print_hex(hba->pi);
    print("\n");

    ahci_detect_drives();
    
    if (port_count > 0) {
        print("AHCI: Initialization completed with ");
        print_hex(port_count);
        print(" drives found\n");
        test_disk_read();
    } else {
        print("AHCI: No drives found\n");
    }
}

int is_fs_supported(uint32_t lba) {
    uint8_t buffer[512];
    if (!ahci_read_sectors(lba, 1, buffer)) {
        return 0;
    }

    if (memcmp(buffer, "\x4C\x57\x53\x4F\x01\x00\x00\x00", 8) == 0) {
        return 1;
    }

    return 0;
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
