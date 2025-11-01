# AlwexOS 🖥️

**An educational operating system designed for learning programming fundamentals with unmatched stability**

## 🎯 Features

- **Dual Boot Support**: Full BIOS and UEFI compatibility
- **Graphics**: VGA and FrameBuffer rendering
- **Unbreakable Design**: Resistant to crashes and system failures
- **Educational Focus**: Perfect for learning low-level programming
- **Built-in Language**: AlwexScript for system programming
- **Lightweight**: Minimal dependencies, maximum performance
- **File System**: This OS first tries to find the disk using AHCI, if it does not find it, it initializes the file system in RAM
- **M.2**: M.2 SSD support is being developed
- **Disk**: To use it, you need the first drive (with OS) and the second drive (for data)
- **AI**: A simple AI
- **Keyboard**: PS/2 is running and USB support is being developed

## 💡 Why This Approach Rocks
## For Students:
- 🚀 Instant gratification - system works immediately

- 🧠 Learn fundamentals without hardware barriers

## For Developers:
- 📦 Modular architecture - easy to add real AHCI later

- 🐛 Easier debugging - consistent behavior across hardware

- 🌐 Hardware agnostic - runs anywhere

## For Educators:
- 👥 Consistent experience - all students see same behavior

- 📚 Controlled environment - perfect for assignments

- ⚡ No setup required - just boot and go

## 🚀 Quick Start

## Recommended OS for the build
- Ubuntu 24.04 and higher

### Dependencies
- GCC
- NASM
- LD
- XORRISO
- MFORMAT
- OBJCOPY
- PARTED
- SFDISK
- GNU-EFI
- QEMU (for emulation)
- OVMF

### Build and Run
```bash
# Clone repository
git clone https://github.com/alwex0920/AlwexOS.git
cd AlwexOS

# Build system
./build.sh
