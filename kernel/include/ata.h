#ifndef ATA_H
#define ATA_H

#include "stdint.h"

uint32_t find_fs_partition(void);
void ata_init(void);
void ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);
void ata_write_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);

#endif