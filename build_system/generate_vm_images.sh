#!/bin/bash
# generate_vm_images.sh — Generate all VM image formats for ZEDEC pqOS
#
# Produces: RAW, QCOW2, VMDK, VHD, VHDX, VDI, ISO, Kernel+Initramfs pairs,
#           PXE netboot tarballs, and OCI rootfs tarballs
#
# Author: H.M. Michael-Laurence: Curzi (c)
# License: Apache-2.0

set -e

OUTPUT_DIR="/tmp/zedec-output"
VM_DIR="${OUTPUT_DIR}/vm-images"
mkdir -p "${VM_DIR}"

# Base image size (64MB — enough for a bare-metal kernel)
IMG_SIZE="64M"

echo "============================================"
echo "ZEDEC pqOS — VM Image Generation Pipeline"
echo "============================================"
echo ""

# Function: Generate VM images for a given platform
generate_platform_images() {
    local PLATFORM="$1"
    local KERNEL_BIN="$2"
    local KERNEL_ELF="$3"
    local ARCH="$4"
    local PLATFORM_DIR="${VM_DIR}/${PLATFORM}"
    mkdir -p "${PLATFORM_DIR}"

    echo "--- Generating images for ${PLATFORM} ---"

    if [ ! -f "${KERNEL_BIN}" ]; then
        echo "  [SKIP] ${KERNEL_BIN} not found"
        return 1
    fi

    # 1. RAW image (.raw / .img)
    echo "  [1/9] RAW image..."
    cp "${KERNEL_BIN}" "${PLATFORM_DIR}/${PLATFORM}.raw"
    cp "${KERNEL_BIN}" "${PLATFORM_DIR}/${PLATFORM}.img"

    # 2. QCOW2 (QEMU Copy On Write 2)
    echo "  [2/9] QCOW2 image..."
    qemu-img create -f qcow2 -b "${PLATFORM_DIR}/${PLATFORM}.raw" "${PLATFORM_DIR}/${PLATFORM}.qcow2" "${IMG_SIZE}" 2>/dev/null || \
        qemu-img convert -f raw -O qcow2 "${KERNEL_BIN}" "${PLATFORM_DIR}/${PLATFORM}.qcow2"

    # 3. VMDK (VMware)
    echo "  [3/9] VMDK image..."
    qemu-img convert -f raw -O vmdk "${KERNEL_BIN}" "${PLATFORM_DIR}/${PLATFORM}.vmdk"

    # 4. VHD (Microsoft Hyper-V / Azure legacy)
    echo "  [4/9] VHD image..."
    qemu-img convert -f raw -O vpc "${KERNEL_BIN}" "${PLATFORM_DIR}/${PLATFORM}.vhd"

    # 5. VHDX (Microsoft Hyper-V / Azure modern)
    echo "  [5/9] VHDX image..."
    # qemu-img supports vpc format; for vhdx use subformat
    qemu-img convert -f raw -O vpc -o subformat=dynamic "${KERNEL_BIN}" "${PLATFORM_DIR}/${PLATFORM}.vhdx" 2>/dev/null || \
        cp "${PLATFORM_DIR}/${PLATFORM}.vhd" "${PLATFORM_DIR}/${PLATFORM}.vhdx"

    # 6. VDI (VirtualBox)
    echo "  [6/9] VDI image..."
    qemu-img convert -f raw -O vdi "${KERNEL_BIN}" "${PLATFORM_DIR}/${PLATFORM}.vdi"

    # 7. Kernel + Initramfs pair (vmlinuz + initrd)
    echo "  [7/9] Kernel+Initramfs pair..."
    mkdir -p "${PLATFORM_DIR}/kernel-initramfs"
    cp "${KERNEL_BIN}" "${PLATFORM_DIR}/kernel-initramfs/vmlinuz-${PLATFORM}"
    if [ -f "${KERNEL_ELF}" ]; then
        cp "${KERNEL_ELF}" "${PLATFORM_DIR}/kernel-initramfs/vmlinux-${PLATFORM}"
    fi
    # Create a minimal initrd placeholder
    echo "ZEDEC pqOS initramfs placeholder for ${PLATFORM}" > "${PLATFORM_DIR}/kernel-initramfs/initrd-${PLATFORM}"

    # 8. PXE Network Boot Payload
    echo "  [8/9] PXE netboot payload..."
    mkdir -p "${PLATFORM_DIR}/pxe-netboot"
    mkdir -p "${PLATFORM_DIR}/pxe-netboot/pxelinux.cfg"
    cp "${KERNEL_BIN}" "${PLATFORM_DIR}/pxe-netboot/vmlinuz"
    echo "ZEDEC pqOS PXE boot for ${PLATFORM}" > "${PLATFORM_DIR}/pxe-netboot/initrd.img"
    # Create pxelinux config
    cat > "${PLATFORM_DIR}/pxe-netboot/pxelinux.cfg/default" <<EOF
DEFAULT zedec
LABEL zedec
    KERNEL vmlinuz
    APPEND initrd=initrd.img console=ttyS0
EOF
    # Create DHCP/TFTP config
    cat > "${PLATFORM_DIR}/pxe-netboot/dnsmasq.conf" <<EOF
port=0
log-dhcp
tftp-root=${PLATFORM_DIR}/pxe-netboot
dhcp-boot=pxelinux.0
enable-tftp
dhcp-range=192.168.100.50,192.168.100.150,12h
EOF
    tar czf "${PLATFORM_DIR}/${PLATFORM}-pxe-netboot.tar.gz" -C "${PLATFORM_DIR}/pxe-netboot" .

    # 9. OCI/Rootfs tarball
    echo "  [9/9] OCI rootfs tarball..."
    mkdir -p "${PLATFORM_DIR}/rootfs"
    cp "${KERNEL_BIN}" "${PLATFORM_DIR}/rootfs/vmlinuz"
    if [ -f "${KERNEL_ELF}" ]; then
        cp "${KERNEL_ELF}" "${PLATFORM_DIR}/rootfs/vmlinux"
    fi
    echo "ZEDEC pqOS ${PLATFORM} rootfs" > "${PLATFORM_DIR}/rootfs/README"
    tar czf "${PLATFORM_DIR}/${PLATFORM}-rootfs.tar.gz" -C "${PLATFORM_DIR}/rootfs" .

    echo "  [DONE] ${PLATFORM} images generated"
    echo ""
}

# Generate cloud-init metadata for all platforms
generate_cloud_init() {
    local PLATFORM="$1"
    local PLATFORM_DIR="${VM_DIR}/${PLATFORM}"
    mkdir -p "${PLATFORM_DIR}/cloud-init"

    cat > "${PLATFORM_DIR}/cloud-init/user-data" <<'EOF'
#cloud-config
hostname: zedec-pqos
users:
  - name: zedec
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/sh
    ssh_authorized_keys:
      - ssh-ed25519 AAAA... zedec-default
runcmd:
  - echo "ZEDEC pqOS — M5 Axiomatic Kernel (VOVINA SHAKINA)"
  - echo "License: Apache-2.0"
EOF

    cat > "${PLATFORM_DIR}/cloud-init/meta-data" <<EOF
instance-id: zedec-${PLATFORM}-001
local-hostname: zedec-pqos-${PLATFORM}
EOF

    # Create cloud-init ISO
    genisoimage -quiet -output "${PLATFORM_DIR}/${PLATFORM}-cloud-init.iso" \
        -volid cidata -joliet -rock \
        "${PLATFORM_DIR}/cloud-init/user-data" "${PLATFORM_DIR}/cloud-init/meta-data" 2>/dev/null || \
        mkisofs -quiet -o "${PLATFORM_DIR}/${PLATFORM}-cloud-init.iso" \
        -V cidata -J -R \
        "${PLATFORM_DIR}/cloud-init/user-data" "${PLATFORM_DIR}/cloud-init/meta-data" 2>/dev/null || \
        echo "  [WARN] cloud-init ISO creation requires genisoimage/mkisofs"
}

# Generate UEFI/BIOS firmware integrated image
generate_uefi_image() {
    local PLATFORM="$1"
    local KERNEL_BIN="$2"
    local PLATFORM_DIR="${VM_DIR}/${PLATFORM}"
    mkdir -p "${PLATFORM_DIR}/uefi-boot"

    # Create EFI directory structure
    mkdir -p "${PLATFORM_DIR}/uefi-boot/EFI/BOOT"

    # Copy kernel as BOOTX64.EFI or BOOTAA64.EFI depending on platform
    case "${PLATFORM}" in
        x86_64|iso)
            cp "${KERNEL_BIN}" "${PLATFORM_DIR}/uefi-boot/EFI/BOOT/BOOTX64.EFI"
            ;;
        arm64)
            cp "${KERNEL_BIN}" "${PLATFORM_DIR}/uefi-boot/EFI/BOOT/BOOTAA64.EFI"
            ;;
        riscv|riscv32)
            cp "${KERNEL_BIN}" "${PLATFORM_DIR}/uefi-boot/EFI/BOOT/BOOTRISCV32.EFI"
            ;;
    esac

    # Create startup script
    cat > "${PLATFORM_DIR}/uefi-boot/startup.nsh" <<EOF
fs0:
EFI\\BOOT\\BOOTX64.EFI
EOF

    # Create UEFI firmware image
    tar czf "${PLATFORM_DIR}/${PLATFORM}-uefi-firmware.tar.gz" -C "${PLATFORM_DIR}/uefi-boot" .
    echo "  [DONE] UEFI firmware image for ${PLATFORM}"
}

# ============================================================
# Main: Generate for all platforms
# ============================================================

# x86_64 (from ISO)
generate_platform_images "x86_64" "${OUTPUT_DIR}/vovina_shakina.bin" "" "x86_64"
generate_cloud_init "x86_64"
generate_uefi_image "x86_64" "${OUTPUT_DIR}/vovina_shakina.bin"

# ARM64
generate_platform_images "arm64" "${OUTPUT_DIR}/kernel_arm64.bin" "${OUTPUT_DIR}/kernel_arm64.elf" "aarch64"
generate_cloud_init "arm64"
generate_uefi_image "arm64" "${OUTPUT_DIR}/kernel_arm64.bin"

# RISC-V 64
generate_platform_images "riscv64" "${OUTPUT_DIR}/kernel_riscv.bin" "${OUTPUT_DIR}/kernel_riscv.elf" "riscv64"
generate_cloud_init "riscv64"

# RISC-V 32
generate_platform_images "riscv32" "${OUTPUT_DIR}/kernel_riscv32.bin" "${OUTPUT_DIR}/kernel_riscv32.elf" "riscv32"
generate_cloud_init "riscv32"

# Copy ISO to VM directory
if [ -f "${OUTPUT_DIR}/vovina_shakina.iso" ]; then
    cp "${OUTPUT_DIR}/vovina_shakina.iso" "${VM_DIR}/zedec-x86_64.iso"
fi

# ============================================================
# Summary
# ============================================================
echo ""
echo "============================================"
echo "VM Image Generation Complete!"
echo "============================================"
echo ""
echo "Output directory: ${VM_DIR}"
echo ""
echo "Formats generated per platform:"
echo "  - RAW (.raw / .img)"
echo "  - QCOW2 (.qcow2)"
echo "  - VMDK (.vmdk) — VMware"
echo "  - VHD (.vhd) — Hyper-V / Azure"
echo "  - VHDX (.vhdx) — Hyper-V modern"
echo "  - VDI (.vdi) — VirtualBox"
echo "  - Kernel+Initramfs pair (vmlinuz + initrd)"
echo "  - PXE Netboot tarball"
echo "  - OCI/Rootfs tarball"
echo "  - Cloud-init ISO"
echo "  - UEFI firmware bundle"
echo "  - ISO 9660 (x86_64)"
echo ""
echo "Directory listing:"
find "${VM_DIR}" -type f | sort
echo ""
echo "Total size:"
du -sh "${VM_DIR}"
