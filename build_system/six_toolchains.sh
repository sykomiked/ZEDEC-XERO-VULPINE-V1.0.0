#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# six_toolchains.sh — every toolchain the six-platform build needs, on a
# fresh Ubuntu 24.04 server (run as root or with sudo). The kernel is
# freestanding, so the cross compilers build the same sources for every
# architecture:
#
#   x86_64  gcc-x86-64-linux-gnu        (elf64)
#   arm64   gcc-aarch64-linux-gnu       (elf64)
#   riscv64 gcc-riscv64-linux-gnu       (elf64, rv64gc)
#   riscv32 clang --target=riscv32-unknown-elf  (elf32, rv32i)
#   arm32   gcc-arm-linux-gnueabihf     (elf32, armv7-a soft-float)
#   wasm32  clang --target=wasm32-unknown-unknown (wasm32)
#
# riscv32 and wasm32 link through lld (wasm-ld / ld.lld) and convert with
# llvm-objcopy; the four gcc targets bring their own binutils. nasm builds
# the x86_64 boot stage, qemu-user runs the boot checks, python3 runs the
# parse-time blob generators (the 144-dimensional lattice ROM, the fractal
# seed, the phase table, the curriculum CAR).
#
# usage: sudo sh build_system/six_toolchains.sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "run with sudo: sudo sh build_system/six_toolchains.sh" >&2
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential make python3 git ca-certificates \
    gcc-x86-64-linux-gnu \
    gcc-aarch64-linux-gnu \
    gcc-riscv64-linux-gnu \
    gcc-arm-linux-gnueabihf \
    libc6-dev-arm64-cross libc6-dev-riscv64-cross libc6-dev-armhf-cross \
    clang lld llvm nasm qemu-user

# the compiler versions, so the build log records them
command -v x86_64-linux-gnu-gcc && x86_64-linux-gnu-gcc --version | head -1
command -v aarch64-linux-gnu-gcc && aarch64-linux-gnu-gcc --version | head -1
command -v riscv64-linux-gnu-gcc && riscv64-linux-gnu-gcc --version | head -1
command -v arm-linux-gnueabihf-gcc && arm-linux-gnueabihf-gcc --version | head -1
clang --version | head -1
echo "[six_toolchains] ready: x86_64 arm64 riscv64 riscv32 arm32 wasm32"