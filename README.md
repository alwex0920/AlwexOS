# AlwexOS 🖥️

**An educational operating system designed for learning programming fundamentals with unmatched stability**

## 🎯 Features

- **Dual Boot Support**: Full BIOS and UEFI compatibility
- **Graphics**: VGA and FrameBuffer rendering
- **Unbreakable Design**: Resistant to crashes and system failures
- **Educational Focus**: Perfect for learning low-level programming
- **Built-in Language**: AlwexScript for system programming
- **Lightweight**: Minimal dependencies, maximum performance
- **File System**: In this OS, all created files are stored in RAM, which makes it possible not to look at the code written, for example, by another student or student, and not to rewrite it, but to come to a solution to the problem yourself.

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
