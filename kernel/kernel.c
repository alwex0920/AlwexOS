#include "include/lib.h"
#include "include/ata.h"
#include "include/fs.h"
#include "include/bootinfo.h"

void kmain(boot_info_t* boot_info) {
    init_framebuffer(boot_info);
    
    if (!boot_info || boot_info->boot_type == BT_BIOS) {
        vga_init();
    }
    serial_init();
    clear_screen();
    ata_init();
    fs_init();    
    shell_main();
    while (1) {
        asm volatile ("hlt");
    }
}