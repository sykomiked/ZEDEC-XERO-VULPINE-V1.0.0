#!/bin/bash
# build_hello.sh — build the ZXV sample user app and regenerate the
# embeddable header. Run from kernel/userapp/.
#
#   cd kernel/userapp && ./build_hello.sh
#
# Produces hello.elf (static AArch64 ELF64, linked at 0x10000) and
# hello_elf.h (byte array embedded by the kernel and seeded to ZXVFS).
set -e
CROSS=${CROSS_COMPILE:-aarch64-linux-gnu-}

${CROSS}gcc -ffreestanding -nostdlib -fno-pie -fno-stack-protector -O2 \
    -Wall -Wextra -c hello.c -o hello.o
${CROSS}ld -T hello.ld -z max-page-size=0x1000 -o hello.elf hello.o

{
  echo "/* hello_elf.h — GENERATED from userapp/hello.elf. Do not edit by hand."
  echo " * Rebuild: see userapp/build_hello.sh. The kernel seeds these bytes to"
  echo " * ZXVFS as 'hello.elf' on first boot so 'run hello.elf' loads from disk."
  echo " */"
  echo "#ifndef HELLO_ELF_H"
  echo "#define HELLO_ELF_H"
  echo "static const unsigned char hello_elf[] = {"
  od -An -v -tx1 hello.elf | sed 's/[0-9a-f]\{2\}/0x&,/g' | sed 's/^/  /'
  echo "};"
  echo "static const unsigned int hello_elf_len = sizeof(hello_elf);"
  echo "#endif /* HELLO_ELF_H */"
} > hello_elf.h

echo "built hello.elf ($(wc -c < hello.elf) bytes) + hello_elf.h"
