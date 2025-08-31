#ifndef AHCI_H
#define AHCI_H

#include "stdint.h"
#include "stddef.h"

typedef struct {
    uint32_t clb;    // Command List Base Address
    uint32_t clbu;   // Command List Base Address Upper
    uint32_t fb;     // FIS Base Address
    uint32_t fbu;    // FIS Base Address Upper
    uint32_t is;     // Interrupt Status
    uint32_t ie;     // Interrupt Enable
    uint32_t cmd;    // Command and Status
    uint32_t rsv0;   // Reserved
    uint32_t tfd;    // Task File Data
    uint32_t sig;    // Signature
    uint32_t ssts;   // SATA Status
    uint32_t sctl;   // SATA Control
    uint32_t serr;   // SATA Error
    uint32_t sact;   // SATA Active
    uint32_t ci;     // Command Issue
    uint32_t sntf;   // SATA Notification
    uint32_t fbs;    // FIS-based Switching Control
    uint32_t rsv1[11];
    uint32_t vendor[4];
} hba_port_t;

typedef struct {
    uint32_t cap;    // Host Capability
    uint32_t ghc;    // Global Host Control
    uint32_t is;     // Interrupt Status
    uint32_t pi;     // Ports Implemented
    uint32_t vs;     // Version
    uint32_t ccc_ctl; // Command Completion Coalescing Control
    uint32_t ccc_pts; // Command Completion Coalescing Ports
    uint32_t em_loc;  // Enclosure Management Location
    uint32_t em_ctl;  // Enclosure Management Control
    uint32_t cap2;    // Host Capability Extended
    uint32_t bohc;    // BIOS/OS Handoff Control and Status
    uint32_t rsv[29];
    uint32_t vendor[32];
    hba_port_t ports[32];
} hba_mem_t;

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t partition_entries_lba;
    uint32_t num_partition_entries;
    uint32_t size_of_partition_entry;
    uint32_t partition_entry_array_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct {
    uint8_t partition_type_guid[16];
    uint8_t unique_partition_guid[16];
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t partition_name[36];
} __attribute__((packed)) gpt_partition_entry_t;

// FIS types
#define FIS_TYPE_REG_H2D    0x27
#define FIS_TYPE_REG_D2H    0x34
#define FIS_TYPE_DMA_ACT    0x39
#define FIS_TYPE_DMA_SETUP  0x41
#define FIS_TYPE_DATA       0x46
#define FIS_TYPE_BIST       0x58
#define FIS_TYPE_PIO_SETUP  0x5F
#define FIS_TYPE_DEV_BITS   0xA1

// FIS - Host to Device
typedef struct {
    uint8_t fis_type;    // FIS_TYPE_REG_H2D
    uint8_t pmport;      // Port multiplier and command
    uint8_t command;     // Command register
    uint8_t feature;     // Feature register
    
    uint8_t lba0;        // LBA low
    uint8_t lba1;        // LBA mid
    uint8_t lba2;        // LBA high
    uint8_t device;      // Device register
    
    uint8_t lba3;        // LBA extended
    uint8_t lba4;        // LBA extended
    uint8_t lba5;        // LBA extended
    uint8_t feature_exp; // Feature extended
    
    uint8_t count;       // Count
    uint8_t count_exp;   // Count extended
    uint8_t icc;         // Isochronous command completion
    uint8_t control;     // Control register
    
    uint8_t rsv[4];      // Reserved
} fis_h2d_t;

// Command Header
typedef struct {
    uint8_t cfl:5;       // Command FIS length in DWORDS
    uint8_t a:1;         // ATAPI
    uint8_t w:1;         // Write
    uint8_t p:1;         // Prefetchable
    
    uint8_t r:1;         // Reset
    uint8_t b:1;         // BIST
    uint8_t c:1;         // Clear busy upon R_OK
    uint8_t rsv0:1;      // Reserved
    uint8_t pmp:4;       // Port multiplier port
    
    uint16_t prdtl;      // Physical region descriptor table length
    
    volatile uint32_t prdbc; // Physical region descriptor byte count
    
    uint32_t ctba;       // Command table descriptor base address
    uint32_t ctbau;      // Command table descriptor base address upper
    uint32_t rsv1[4];    // Reserved
} hba_cmd_header_t;

// Command Table
typedef struct {
    uint8_t cfis[64];    // Command FIS
    uint8_t acmd[16];    // ATAPI command
    uint8_t rsv[48];     // Reserved
    
    // Physical region descriptor table
    struct {
        uint32_t dba;    // Data base address
        uint32_t dbau;   // Data base address upper
        uint32_t rsv;    // Reserved
        uint32_t dbc;    // Byte count
    } prdt[];
} hba_cmd_table_t;

void ahci_init();
int ahci_read_sectors(uint64_t lba, uint32_t count, void* buffer);
int ahci_write_sectors(uint64_t lba, uint32_t count, void* buffer);
void ahci_detect_drives();
uint32_t find_fs_partition();
int is_fs_supported(uint32_t lba);

#endif
