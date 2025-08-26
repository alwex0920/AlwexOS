#include "include/ata.h"
#include "include/lib.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE_SEL   0x1F6
#define ATA_STATUS      0x1F7
#define ATA_CMD         0x1F7

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_RDY  0x40
#define ATA_STATUS_DRQ  0x08

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

static int ata_wait() {
    int timeout = 100000;
    while (inb(ATA_STATUS) & ATA_STATUS_BSY && timeout--);
    if (timeout <= 0) return 0;
    
    timeout = 100000;
    while (!(inb(ATA_STATUS) & ATA_STATUS_RDY) && timeout--);
    return timeout > 0;
}

void ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer) {
    if (!ata_wait()) return;
    
    outb(ATA_DRIVE_SEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, count);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_CMD, 0x20);
    
    for (uint8_t i = 0; i < count; i++) {
        if (!ata_wait()) return;
        while (!(inb(ATA_STATUS) & ATA_STATUS_DRQ));
        
        for (int j = 0; j < 256; j++) {
            uint16_t data = inw(ATA_DATA);
            buffer[j * 2] = data & 0xFF;
            buffer[j * 2 + 1] = (data >> 8) & 0xFF;
        }
        buffer += 512;
    }
}

void ata_write_sectors(uint32_t lba, uint8_t count, uint8_t *buffer) {
    if (!ata_wait()) return;
    
    outb(ATA_DRIVE_SEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, count);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_CMD, 0x30);
    
    for (uint8_t i = 0; i < count; i++) {
        if (!ata_wait()) return;
        while (!(inb(ATA_STATUS) & ATA_STATUS_DRQ));
        
        for (int j = 0; j < 256; j++) {
            uint16_t data = buffer[j * 2] | (buffer[j * 2 + 1] << 8);
            outw(ATA_DATA, data);
        }
        buffer += 512;
    }

    outb(ATA_CMD, 0xE7);
    ata_wait();
}

uint32_t find_fs_partition() {
    uint8_t buffer[512];
    
    ata_read_sectors(0, 1, buffer);

    if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
        print("Found MBR partition table\n");

        for (int i = 0; i < 4; i++) {
            uint8_t* partition_entry = buffer + 446 + i * 16;

            if (partition_entry[4] != 0) {
                uint32_t lba = *(uint32_t*)(partition_entry + 8);
                print("Found MBR partition at LBA: ");
                print_hex(lba);
                print("\n");
                return lba;
            }
        }
        
        print("No active partition found in MBR\n");
        return 0;
    }

    ata_read_sectors(1, 1, buffer);
    gpt_header_t* header = (gpt_header_t*)buffer;

    if (header->signature == 0x5452415020494645ULL) {
        print("Found GPT partition table\n");

        uint32_t partition_table_lba = header->partition_entries_lba;
        ata_read_sectors(partition_table_lba, 1, buffer);
        
        gpt_partition_entry_t* partitions = (gpt_partition_entry_t*)buffer;

        for (int i = 0; i < header->num_partition_entries; i++) {
            uint8_t data_partition_guid[16] = {
                0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
                0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
            };
            
            if (memcmp(partitions[i].partition_type_guid, data_partition_guid, 16) == 0) {
                print("Found GPT data partition at LBA: ");
                print_hex(partitions[i].starting_lba);
                print("\n");
                return partitions[i].starting_lba;
            }
        }
        
        print("No data partition found in GPT\n");
        return 0;
    }
    
    print("No valid partition table found\n");
    return 0;
}

int verify_fs_signature(uint32_t lba) {
    uint8_t buffer[512];
    ata_read_sectors(lba, 1, buffer);

    uint8_t expected_signature[8] = {0x4C, 0x57, 0x53, 0x4F, 0x01, 0x00, 0x00, 0x00};
    
    if (memcmp(buffer, expected_signature, 8) == 0) {
        return 1;
    }
    
    return 0;
}

void ata_init() {
    outb(ATA_DRIVE_SEL, 0xA0);
    uint8_t status = inb(ATA_STATUS);
    
    print("ATA status: ");
    print_hex(status);
    print("\n");
    
    if (status == 0xFF) {
        print("No ATA disk detected\n");
        return;
    }
    
    if (!ata_wait()) {
        print("ATA disk timeout\n");
        return;
    }

    print("Searching for filesystem partition...\n");
    uint32_t fs_lba = find_fs_partition();
    
    if (fs_lba == 0) {
        print("No filesystem partition found\n");
        return;
    }

    print("Verifying filesystem signature...\n");
    if (!verify_fs_signature(fs_lba)) {
        print("Invalid filesystem signature\n");
        return;
    }
    
    char debug_msg[50];
    itoa(fs_lba, debug_msg, 10);
    print("Found valid partition at LBA: ");
    print(debug_msg);
    print("\n");

    print("ATA disk and filesystem initialized\n");
}