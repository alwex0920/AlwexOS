#include "include/lib.h"
#include "include/ahci.h"
#include "include/fs.h"
#include "include/bootinfo.h"

extern uint32_t _end;

void kmain(boot_info_t* boot_info) {
    init_framebuffer(boot_info);
    
    if (!boot_info || boot_info->boot_type == BT_BIOS) {
        vga_init();
    }
    serial_init();
    clear_screen();
    uint32_t heap_start = (uint32_t)&_end;
    uint32_t heap_size = 16 * 1024 * 1024;
    mm_init(heap_start, heap_size);
    ahci_init();
    uint32_t fs_lba = find_fs_partition();
    
    if (fs_lba == 0) {
        print("No filesystem partition found. Trying to use first available sector...\n");
        
        fs_lba = 1;

        uint8_t test_buffer[512] = {0};
        if (ahci_write_sectors(fs_lba, 1, test_buffer)) {
            print("Disk is writable. Formatting...\n");
            if (format_disk(fs_lba)) {
                print("Disk formatted successfully.\n");
                fs_init(fs_lba);
            } else {
                print("Formatting failed. Using ramdisk.\n");
                fs_init_ramdisk();
            }
        } else {
            print("Disk not writable. Using ramdisk.\n");
            fs_init_ramdisk();
        }
    } else {
        print("Filesystem found at LBA: ");
        print_hex(fs_lba);
        print("\n");
        fs_init(fs_lba);
    }    
    shell_main();
    while (1) {
        asm volatile ("hlt");
    }
}
