#!/bin/bash
set -euo pipefail

OUTPUT_ISO="AlwexOS.iso"
KERNEL_SRCDIR="kernel"
KERNEL_SOURCES=("$KERNEL_SRCDIR"/*.c)
BOOTLOADER_ASM="boot/bootloader.asm"
UEFI_BOOT_SRC="boot/uefi_boot.c"

cleanup() {
    echo "Cleaning up..."
    sudo umount -f /mnt/efi 2>/dev/null || true
    sudo umount -f /mnt/bios 2>/dev/null || true
    sudo losetup -D 2>/dev/null || true
    rm -rf build/bios_root
}

trap cleanup EXIT INT TERM

echo "=== Building AlwexOS 64-bit ==="

# Проверка зависимостей
echo "[1/8] Checking dependencies..."
command -v nasm >/dev/null || { echo >&2 "Missing nasm. Install: sudo apt install nasm"; exit 1; }
command -v gcc >/dev/null || { echo >&2 "Missing gcc. Install: sudo apt install gcc"; exit 1; }
command -v ld >/dev/null || { echo >&2 "Missing binutils. Install: sudo apt install binutils"; exit 1; }
command -v xorriso >/dev/null || { echo >&2 "Missing xorriso. Install: sudo apt install xorriso"; exit 1; }
command -v mformat >/dev/null || { echo >&2 "Missing mtools. Install: sudo apt install mtools"; exit 1; }
command -v objcopy >/dev/null || { echo >&2 "Missing objcopy. Install: sudo apt install binutils"; exit 1; }
command -v parted >/dev/null || { echo >&2 "Missing parted. Install: sudo apt install parted"; exit 1; }
command -v sfdisk >/dev/null || { echo >&2 "Missing sfdisk. Install: sudo apt install util-linux"; exit 1; }

# Установка путей для gnu-efi
echo "Setting up gnu-efi paths..."
EFI_CC="gcc"
EFI_LD="ld"

if [ -f "/usr/lib/x86_64-linux-gnu/gnuefi/crt0-efi-x86_64.o" ]; then
    EFI_INCDIR="/usr/include/efi"
    EFI_CRT0="/usr/lib/x86_64-linux-gnu/gnuefi/crt0-efi-x86_64.o"
    EFI_LDS="/usr/lib/x86_64-linux-gnu/gnuefi/elf_x86_64_efi.lds"
    EFI_LIBDIR="/usr/lib/x86_64-linux-gnu"
elif [ -f "/usr/lib/crt0-efi-x86_64.o" ]; then
    EFI_INCDIR="/usr/include/efi"
    EFI_CRT0="/usr/lib/crt0-efi-x86_64.o"
    EFI_LDS="/usr/lib/elf_x86_64_efi.lds"
    EFI_LIBDIR="/usr/lib"
else
    echo "Error: gnu-efi files not found. Please install gnu-efi:"
    echo "sudo apt install gnu-efi"
    exit 1
fi

if [ ! -f "$EFI_CRT0" ]; then
    echo "Error: EFI CRT0 file not found: $EFI_CRT0"
    exit 1
fi
if [ ! -f "$EFI_LDS" ]; then
    echo "Error: EFI linker script not found: $EFI_LDS"
    exit 1
fi

echo "Using gnu-efi: CRT0=$EFI_CRT0, LDS=$EFI_LDS"

# Создаем директорию сборки
mkdir -p build

# Сборка BIOS загрузчика
echo "[2/8] Building BIOS bootloader..."
nasm -f bin "$BOOTLOADER_ASM" -o build/bootloader.bin

if [ "$(xxd -s 510 -l 2 -p build/bootloader.bin)" != "55aa" ]; then
    echo "Error: Bootloader missing 0xAA55 signature"
    exit 1
fi

# Сборка ядра
echo "[3/8] Building 64-bit kernel..."
mkdir -p build/kernel

echo "  Compiling entry.asm..."
nasm -f elf64 kernel/entry.asm -o build/entry.o

OBJ_FILES=(build/entry.o)
for src in "${KERNEL_SOURCES[@]}"; do
    obj_name="build/$(basename "${src%.c}").o"
    echo "  Compiling $src..."
    gcc -m64 -ffreestanding -O2 -c "$src" -o "$obj_name" \
        -Iinclude -nostdinc -fno-stack-protector -fno-pic -fno-pie
    OBJ_FILES+=("$obj_name")
done

echo "  Checking for _start symbol..."
if ! nm build/entry.o | grep -q "_start"; then
    echo "Error: _start symbol not found in entry.o"
    echo "Make sure entry.asm contains 'global _start' and '_start:' label"
    exit 1
fi

echo "  Creating linker script..."
cat > build/linker.ld << 'EOF'
ENTRY(_start)

SECTIONS {
    /* Явно устанавливаем начальный адрес */
    . = 0x200000;
    
    /* Секция начала с явным выравниванием */
    .text.start : {
        build/entry.o(.text.start)
    }
    
    /* Основные секции */
    .text : {
        *(.text)
        *(.text.*)
    }
    
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data : {
        *(.data)
        *(.data.*)
    }
    
    /* Секция BSS с стеком */
    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
        
        /* Выравнивание стека */
        . = ALIGN(16);
        stack_bottom = .;
        . += 128 * 1024; /* 128KB stack */
        stack_top = .;
    }
    
    . = ALIGN(4096);
    _end = .;

    /* Отбрасываем ненужные секции */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }
}
EOF

echo "[4/8] Linking kernel..."
ld -m elf_x86_64 -nostdlib -T build/linker.ld -z noexecstack -z max-page-size=0x1000 -o build/kernel.elf "${OBJ_FILES[@]}"

echo "[5/8] Generating debug information..."
objdump -D build/kernel.elf > build/kernel.disasm
nm build/kernel.elf > build/kernel.sym
readelf -S build/kernel.elf > build/kernel.sections

entry_point=$(nm build/kernel.elf | grep " _start$" | awk '{print $1}')
if [ -z "$entry_point" ]; then
    echo "ERROR: _start symbol not found in kernel"
    nm build/kernel.elf | grep start
    exit 1
else
    echo "  Kernel entry point: 0x$entry_point"
fi

echo "[6/8] Building UEFI bootloader..."
mkdir -p build/efi/boot

echo "  Compiling UEFI bootloader..."
gcc -m64 -mno-red-zone -fno-stack-protector -fpic -fshort-wchar \
    -I"$EFI_INCDIR" -I"$EFI_INCDIR/x86_64" \
    -c "$UEFI_BOOT_SRC" -o build/uefi_boot.o
        
echo "  Linking UEFI bootloader..."
ld -T "$EFI_LDS" -shared -Bsymbolic -nostdlib \
    "$EFI_CRT0" build/uefi_boot.o \
    -o build/efi/boot/bootx64.efi \
    -L"$EFI_LIBDIR" -lgnuefi -lefi
        
echo "  Stripping UEFI bootloader..."
objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
    -j .rela -j .reloc --target=efi-app-x86_64 build/efi/boot/bootx64.efi

echo "[7/8] Creating disk images..."

echo "  Creating EFI image..."
dd if=/dev/zero of=build/efiboot.img bs=1M count=64 status=none
mkfs.fat -F 32 build/efiboot.img >/dev/null

echo "  Mounting EFI image to copy files..."
sudo mkdir -p /mnt/efi
sudo mount -o loop build/efiboot.img /mnt/efi
sudo mkdir -p /mnt/efi/EFI/BOOT
sudo cp build/efi/boot/bootx64.efi /mnt/efi/EFI/BOOT/
sudo cp build/kernel.elf /mnt/efi/
echo '\\EFI\\BOOT\\BOOTX64.EFI' | sudo tee /mnt/efi/startup.nsh > /dev/null
sudo umount /mnt/efi

echo "  Creating BIOS image..."
mkdir -p build/bios_root
cp build/kernel.elf build/bios_root/kernel.elf

dd if=/dev/zero of=build/boot.img bs=512 count=2880 status=none
mkfs.fat -F 12 -n "ALWEXOS" build/boot.img >/dev/null

echo "  Mounting BIOS image to copy files..."
sudo mkdir -p /mnt/bios
sudo mount -o loop build/boot.img /mnt/bios
sudo cp build/bios_root/kernel.elf /mnt/bios/
sudo umount /mnt/bios

dd if=build/bootloader.bin of=build/boot.img conv=notrunc bs=512 count=1 status=none

echo "  Creating data partition with GPT..."
dd if=/dev/zero of=build/data.img bs=1M count=32 status=none

# Создаем GPT разметку
sudo parted build/data.img mklabel gpt
sudo parted build/data.img mkpart primary fat32 1MiB 100%

# Настраиваем loop-устройство
LOOP_DEV=$(sudo losetup -f --show -P build/data.img)
sleep 2

if [ ! -b "${LOOP_DEV}p1" ]; then
    echo "Error: Partition not found: ${LOOP_DEV}p1"
    sudo losetup -d $LOOP_DEV
    exit 1
fi

# Форматируем раздел в FAT32
sudo mkfs.fat -F 32 -n "ALWEXDATA" ${LOOP_DEV}p1

# Монтируем и создаем файловую систему
sudo mkdir -p /mnt/data
sudo mount ${LOOP_DEV}p1 /mnt/data

# Создаем структуру файловой системы
sudo mkdir -p /mnt/data/system
sudo mkdir -p /mnt/data/users
sudo mkdir -p /mnt/data/temp

# Записываем сигнатуру файловой системы в первый сектор раздела
# Это поможет ОС идентифицировать раздел
echo -n -e '\x4C\x57\x53\x4F\x01\x00\x00\x00' | sudo dd of=${LOOP_DEV}p1 bs=512 count=1 conv=notrunc

# Создаем файл с информацией о файловой системе
echo "AlwexOS Filesystem v1.0" | sudo tee /mnt/data/system/fsinfo.txt > /dev/null

sudo umount /mnt/data
sudo losetup -d $LOOP_DEV

rm -rf build/bios_root

echo "[8/8] Creating ISO image..."
mkdir -p build/iso

cp build/boot.img build/iso/
cp build/efiboot.img build/iso/
cp build/data.img build/iso/
cp build/kernel.elf build/iso/
mkdir -p build/iso/EFI/BOOT
cp build/efi/boot/bootx64.efi build/iso/EFI/BOOT/
echo '\\EFI\\BOOT\\BOOTX64.EFI' > build/iso/startup.nsh

xorriso -as mkisofs \
    -iso-level 3 \
    -full-iso9660-filenames \
    -volid "ALWEX_OS" \
    -eltorito-boot boot.img \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    -eltorito-alt-boot \
    -e efiboot.img \
    -no-emul-boot \
    -isohybrid-gpt-basdat \
    -o "$OUTPUT_ISO" \
    build/iso

echo "=== Build successful! ==="
echo "ISO image: $OUTPUT_ISO"

read -p "Do you want to test in QEMU? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Testing with QEMU (UEFI)..."
    OVMF_PATH="/usr/share/OVMF/OVMF_CODE_4M.fd"
    if [ ! -f "$OVMF_PATH" ]; then
        OVMF_PATH="/usr/share/ovmf/OVMF.fd"
        if [ ! -f "$OVMF_PATH" ]; then
            echo "OVMF not found. Please install OVMF:"
            echo "sudo apt install ovmf"
            exit 1
        fi
    fi

    qemu-system-x86_64 \
        -machine q35 \
        -cdrom "$OUTPUT_ISO" \
        -drive if=pflash,format=raw,readonly=on,file="$OVMF_PATH" \
        -drive file=build/data.img,format=raw,index=1,media=disk \
        -m 2048 \
        -serial stdio \
        -debugcon file:debug.log \
        -net none \
        -boot order=d \
        -no-reboot
fi
