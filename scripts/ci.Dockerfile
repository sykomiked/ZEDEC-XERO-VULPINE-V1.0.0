# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# The image scripts/verify-local.sh --docker runs CI's Linux checks in:
# the packages .github/workflows/ci.yml installs on its Ubuntu runners, with
# gcc 11 as gcc/cc (like host-tests on ubuntu-22.04) and clang 18 for the
# sanitizers (like the ubuntu-24.04 sanitizer jobs). Build context: scripts/.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git make python3 python3-pip ccache \
        gcc-11 gcc clang llvm libclang-rt-18-dev libxml2-utils valgrind \
        nasm xorriso grub-pc-bin grub-common mtools \
        qemu-system-arm qemu-system-x86 qemu-system-misc qemu-user \
        gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu gcc-arm-linux-gnueabihf \
        gcc-i686-linux-gnu gcc-s390x-linux-gnu \
        libc6-dev-arm64-cross libc6-dev-riscv64-cross libc6-dev-armhf-cross \
        libc6-dev-i386-cross libc6-dev-s390x-cross \
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf /usr/bin/gcc-11 /usr/local/bin/gcc \
    && ln -sf /usr/bin/gcc-11 /usr/local/bin/cc \
    && python3 -m pip install --break-system-packages --no-cache-dir clang-format==18.1.8 \
    && /usr/sbin/update-ccache-symlinks
