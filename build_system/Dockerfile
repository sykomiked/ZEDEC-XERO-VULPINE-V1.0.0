# Dockerfile — ZEDEC pqOS Kernel Build Environment
# Cross-compiles the VOVINA SHAKINA kernel to x86 32-bit and builds a bootable ISO
# Author: H.M. Michael-Laurence: Curzi (c)
FROM --platform=linux/amd64 ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc-multilib \
    g++-multilib \
    nasm \
    xorriso \
    grub-pc-bin \
    grub-common \
    qemu-system-x86 \
    mtools \
    && rm -rf /var/lib/apt/lists/*

# Create build workspace
WORKDIR /zedec-build

# Copy kernel source
COPY . /zedec-build/

# Default: build ISO
CMD ["bash", "build_iso.sh"]
