#include <efi.h>
#include <efilib.h>
#include "../kernel/include/bootinfo.h"

#define KERNEL_FILE_NAME L"kernel.elf"

// Структуры ELF
typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"UEFI Bootloader starting...\n");
    
    EFI_STATUS status;
    EFI_LOADED_IMAGE *loaded_image = NULL;
    EFI_FILE_IO_INTERFACE *fs;
    EFI_FILE_HANDLE root_dir, kernel_file;
    UINTN file_size = 0;
    
    // Получаем информацию о загруженном образе
    status = uefi_call_wrapper(BS->HandleProtocol, 3, 
        ImageHandle, &gEfiLoadedImageProtocolGuid, (void**)&loaded_image);
    if (EFI_ERROR(status)) {
        Print(L"Error getting loaded image: %r\n", status);
        return status;
    }
    
    // Открываем файловую систему
    status = uefi_call_wrapper(BS->HandleProtocol, 3, 
        loaded_image->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&fs);
    if (EFI_ERROR(status)) {
        Print(L"Error accessing filesystem: %r\n", status);
        return status;
    }
    
    // Открываем корневой раздел
    status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root_dir);
    if (EFI_ERROR(status)) {
        Print(L"Error opening volume: %r\n", status);
        return status;
    }
    
    // Открываем файл ядра
    status = uefi_call_wrapper(root_dir->Open, 5, root_dir, &kernel_file, 
        KERNEL_FILE_NAME, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        Print(L"Error opening kernel file: %r\n", status);
        return status;
    }
    
    // Получаем размер файла
    EFI_FILE_INFO *file_info;
    UINTN info_size = sizeof(EFI_FILE_INFO) + 128;
    status = uefi_call_wrapper(BS->AllocatePool, 3, 
        EfiLoaderData, info_size, (void**)&file_info);
    if (EFI_ERROR(status)) {
        Print(L"Memory allocation error: %r\n", status);
        return status;
    }
    
    status = uefi_call_wrapper(kernel_file->GetInfo, 4, kernel_file, 
        &gEfiFileInfoGuid, &info_size, file_info);
    if (EFI_ERROR(status)) {
        Print(L"Error getting file info: %r\n", status);
        uefi_call_wrapper(BS->FreePool, 1, file_info);
        return status;
    }
    
    file_size = file_info->FileSize;
    uefi_call_wrapper(BS->FreePool, 1, file_info);
    
    Print(L"Kernel size: %d bytes\n", file_size);
    
    // Читаем ELF-заголовок
    Elf64_Ehdr ehdr;
    UINTN read_size = sizeof(ehdr);
    status = uefi_call_wrapper(kernel_file->Read, 3, kernel_file, &read_size, &ehdr);
    if (EFI_ERROR(status)) {
        Print(L"Error reading ELF header: %r\n", status);
        return status;
    }
    
    // Проверяем сигнатуру ELF
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' || 
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        Print(L"Not a valid ELF file\n");
        return EFI_LOAD_ERROR;
    }
    
    // Проверяем архитектуру
    if (ehdr.e_machine != 0x3E) { // EM_X86_64
        Print(L"Not x86_64 executable\n");
        return EFI_LOAD_ERROR;
    }
    
    Print(L"ELF entry point: 0x%llx\n", ehdr.e_entry);
    
    // Читаем заголовки программ
    UINTN phdr_size = ehdr.e_phnum * ehdr.e_phentsize;
    Elf64_Phdr *phdrs;
    status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, phdr_size, (void**)&phdrs);
    if (EFI_ERROR(status)) {
        Print(L"Error allocating memory for program headers: %r\n", status);
        return status;
    }
    
    status = uefi_call_wrapper(kernel_file->SetPosition, 2, kernel_file, ehdr.e_phoff);
    if (EFI_ERROR(status)) {
        Print(L"Error seeking to program headers: %r\n", status);
        uefi_call_wrapper(BS->FreePool, 1, phdrs);
        return status;
    }
    
    read_size = phdr_size;
    status = uefi_call_wrapper(kernel_file->Read, 3, kernel_file, &read_size, phdrs);
    if (EFI_ERROR(status)) {
        Print(L"Error reading program headers: %r\n", status);
        uefi_call_wrapper(BS->FreePool, 1, phdrs);
        return status;
    }
    
    // Загружаем сегменты
    EFI_PHYSICAL_ADDRESS kernel_entry = ehdr.e_entry;
    
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr *phdr = &phdrs[i];
        if (phdr->p_type != 1) continue; // PT_LOAD
            
        Print(L"Loading segment at 0x%llx, size 0x%llx\n", phdr->p_vaddr, phdr->p_memsz);
        
        // Выделяем память по указанному адресу
        EFI_PHYSICAL_ADDRESS seg_addr = phdr->p_vaddr;
        UINTN pages = EFI_SIZE_TO_PAGES(phdr->p_memsz);
        
        // Сначала пробуем выделить по указанному адресу
        status = uefi_call_wrapper(BS->AllocatePages, 4, 
            AllocateAddress, EfiLoaderData, pages, &seg_addr);
        
        // Если не удалось, пробуем любой адрес
        if (EFI_ERROR(status)) {
            seg_addr = 0;
            status = uefi_call_wrapper(BS->AllocatePages, 4, 
                AllocateAnyPages, EfiLoaderData, pages, &seg_addr);
            if (EFI_ERROR(status)) {
                Print(L"Error allocating pages for segment: %r\n", status);
                uefi_call_wrapper(BS->FreePool, 1, phdrs);
                return status;
            }
        }
        
        // Читаем данные сегмента в выделенную память
        status = uefi_call_wrapper(kernel_file->SetPosition, 2, kernel_file, phdr->p_offset);
        if (EFI_ERROR(status)) {
            Print(L"Error seeking to segment: %r\n", status);
            uefi_call_wrapper(BS->FreePool, 1, phdrs);
            return status;
        }
        
        read_size = phdr->p_filesz;
        status = uefi_call_wrapper(kernel_file->Read, 3, kernel_file, &read_size, (void*)seg_addr);
        if (EFI_ERROR(status)) {
            Print(L"Error reading segment: %r\n", status);
            uefi_call_wrapper(BS->FreePool, 1, phdrs);
            return status;
        }
        
        // Обнуляем BSS
        if (phdr->p_memsz > phdr->p_filesz) {
            UINTN bss_size = phdr->p_memsz - phdr->p_filesz;
            SetMem((void*)(seg_addr + phdr->p_filesz), bss_size, 0);
        }
        
        // Корректируем точку входа, если она находится в этом сегменте
        if (kernel_entry >= phdr->p_vaddr && 
            kernel_entry < phdr->p_vaddr + phdr->p_memsz) {
            kernel_entry = seg_addr + (kernel_entry - phdr->p_vaddr);
            Print(L"Adjusted entry point to: 0x%llx\n", kernel_entry);
        }
    }
    
    uefi_call_wrapper(BS->FreePool, 1, phdrs);
    
    // Закрываем файлы
    uefi_call_wrapper(kernel_file->Close, 1, kernel_file);
    uefi_call_wrapper(root_dir->Close, 1, root_dir);
    
    // Выходим из boot services
    Print(L"Exiting boot services...\n");
    
    UINTN mem_map_size = 0;
    UINTN mem_map_key;
    UINTN descriptor_size;
    UINT32 descriptor_version;
    
    // Получаем размер карты памяти
    status = uefi_call_wrapper(BS->GetMemoryMap, 5, 
        &mem_map_size, NULL, &mem_map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL) {
        Print(L"Error getting memory map size: %r\n", status);
        return status;
    }
    
    // Выделяем память под карту памяти
    mem_map_size += 2 * descriptor_size;
    EFI_MEMORY_DESCRIPTOR *mem_map;
    status = uefi_call_wrapper(BS->AllocatePool, 3, 
        EfiLoaderData, mem_map_size, (void**)&mem_map);
    if (EFI_ERROR(status)) {
        Print(L"Error allocating memory map: %r\n", status);
        return status;
    }
    
    // Получаем карту памяти
    status = uefi_call_wrapper(BS->GetMemoryMap, 5, 
        &mem_map_size, mem_map, &mem_map_key, &descriptor_size, &descriptor_version);
    if (EFI_ERROR(status)) {
        Print(L"Error getting memory map: %r\n", status);
        uefi_call_wrapper(BS->FreePool, 1, mem_map);
        return status;
    }
    
    // Получаем информацию о фреймбуфере
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    boot_info_t boot_info = {0};
    
    status = uefi_call_wrapper(BS->LocateProtocol, 3, &gop_guid, NULL, (void**)&gop);
    if (!EFI_ERROR(status) && gop) {
        boot_info.framebuffer_base = (void*)gop->Mode->FrameBufferBase;
        boot_info.framebuffer_width = gop->Mode->Info->HorizontalResolution;
        boot_info.framebuffer_height = gop->Mode->Info->VerticalResolution;
        boot_info.framebuffer_pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
    }
    
    // Выходим из boot services
    status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, mem_map_key);
    if (EFI_ERROR(status)) {
        Print(L"Error exiting boot services: %r\n", status);
        uefi_call_wrapper(BS->FreePool, 1, mem_map);
        return status;
    }
    
    boot_info.system_table = SystemTable;
    boot_info.memory_map = mem_map;
    boot_info.memory_map_size = mem_map_size;
    boot_info.descriptor_size = descriptor_size;
    boot_info.boot_type = BT_UEFI;

    // Переходим к ядру
    asm volatile (
        "cli\n\t"
        "mov %0, %%rax\n\t"
        "mov %1, %%rdi\n\t"
        "jmp *%%rax\n\t"
        : 
        : "r" (kernel_entry), "r" (&boot_info)
        : "rax", "rdi", "memory"
    );
    
    while(1) {
        asm volatile ("hlt");
    }
    
    return EFI_SUCCESS;
}