# Build Dependencies

This document lists the exact toolchain versions required to build ZXV on different platforms.

## macOS (Homebrew)

### ARM64 Build

```bash
brew install aarch64-linux-gnu gcc qemu xorriso
```

**Known Working Versions:**
- `aarch64-linux-gnu`: 13.2.0
- `gcc`: 14.0.0
- `qemu`: 8.2.0
- `xorriso`: 1.5.6

### x86_64 Build

```bash
brew install gcc qemu xorriso grub
```

**Known Working Versions:**
- `gcc`: 14.0.0
- `qemu`: 8.2.0
- `xorriso`: 1.5.6
- `grub`: 2.12

### RISC-V Build

```bash
brew install riscv64-elf-gcc qemu
```

**Known Working Versions:**
- `riscv64-elf-gcc`: 13.2.0
- `qemu`: 8.2.0

## Linux (Debian/Ubuntu 22.04)

### ARM64 Build

```bash
sudo apt-get update
sudo apt-get install -y gcc-aarch64-linux-gnu qemu-system-arm xorriso
```

**Known Working Versions:**
- `gcc-aarch64-linux-gnu`: 12.2.0
- `qemu-system-arm`: 7.0.0
- `xorriso`: 1.5.4

### x86_64 Build

```bash
sudo apt-get update
sudo apt-get install -y gcc qemu-system-x86 xorriso grub-pc-bin grub-common
```

**Known Working Versions:**
- `gcc`: 12.2.0
- `qemu-system-x86`: 7.0.0
- `xorriso`: 1.5.4
- `grub-pc-bin`: 2.06-3ubuntu7

### RISC-V Build

```bash
sudo apt-get update
sudo apt-get install -y gcc-riscv64-linux-gnu qemu-system-riscv64
```

**Known Working Versions:**
- `gcc-riscv64-linux-gnu`: 12.2.0
- `qemu-system-riscv64`: 7.0.0

### RISC-V 32-bit Build

```bash
sudo apt-get update
sudo apt-get install -y gcc-riscv32-linux-gnu qemu-system-riscv32
```

**Known Working Versions:**
- `gcc-riscv32-linux-gnu`: 12.2.0
- `qemu-system-riscv32`: 7.0.0

### ARM32 Build

```bash
sudo apt-get update
sudo apt-get install -y gcc-arm-linux-gnueabihf qemu-system-arm
```

**Known Working Versions:**
- `gcc-arm-linux-gnueabihf`: 12.2.0
- `qemu-system-arm`: 7.0.0

## Linux (Arch Linux)

### ARM64 Build

```bash
sudo pacman -S aarch64-linux-gnu-gcc qemu-system-arm xorriso
```

**Known Working Versions:**
- `aarch64-linux-gnu-gcc`: 13.2.1
- `qemu-system-arm`: 8.2.0
- `xorriso`: 1.5.6

### x86_64 Build

```bash
sudo pacman -S gcc qemu-system-x86 xorriso grub
```

**Known Working Versions:**
- `gcc`: 13.2.1
- `qemu-system-x86`: 8.2.0
- `xorriso`: 1.5.6
- `grub`: 2:2.12.r4.g6573b9693-1

### RISC-V Build

```bash
sudo pacman -S riscv64-linux-gnu-gcc qemu-system-riscv64
```

**Known Working Versions:**
- `riscv64-linux-gnu-gcc`: 13.2.1
- `qemu-system-riscv64`: 8.2.0

## Minimum Requirements

### GCC

- Minimum version: 11.0
- Recommended version: 12.0 or later
- Required C standard support: C11

### QEMU

- Minimum version: 7.0
- Recommended version: 8.0 or later
- Required architectures: aarch64, x86_64, riscv64, riscv32, arm

### Xorriso

- Minimum version: 1.5.0
- Required for: ISO generation (x86_64)

### GRUB

- Minimum version: 2.06
- Required for: x86_64 multiboot boot

## Verification

To verify your toolchain versions:

```bash
# GCC
gcc --version
aarch64-linux-gnu-gcc --version

# QEMU
qemu-system-aarch64 --version
qemu-system-x86_64 --version

# Xorriso
xorriso --version

# GRUB
grub-install --version
```

## Troubleshooting

### "Command not found" errors

Ensure the toolchain is in your PATH. On macOS with Homebrew, you may need:

```bash
export PATH="/opt/homebrew/bin:$PATH"
```

### "Unsupported architecture" errors

Ensure your QEMU version supports the target architecture. Check with:

```bash
qemu-system-<arch> --version
```

### "Undefined reference" errors

This usually indicates a toolchain version mismatch. Ensure you're using the recommended versions listed above.

### "Permission denied" on build.sh

Make the script executable:

```bash
chmod +x build.sh
```
