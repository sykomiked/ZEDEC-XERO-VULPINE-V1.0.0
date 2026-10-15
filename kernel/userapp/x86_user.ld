/* x86_user.ld: link an x86-64 ring-3 program into ZXV user space.
 *
 * USER_CODE_BASE (arch/x86_64/x86_userspace.h): PML4 slot 255 + 64 KB.
 * Text and read-only data share one R+X segment; data and BSS start on the
 * next page in an R+W segment, so the loader's W^X check holds and no page
 * is mapped twice.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5); /* R+X */
    data PT_LOAD FLAGS(6); /* R+W */
}

SECTIONS
{
    /DISCARD/ : {
        *(.note.*)
        *(.comment)
        *(.eh_frame*)
    }

    . = 0x00007F8000010000;
    .text : { *(.text.startup) *(.text) *(.text.*) } :text
    .rodata : { *(.rodata) *(.rodata.*) } :text

    . = ALIGN(0x1000);
    .data : { *(.data) *(.data.*) } :data
    .bss : { *(.bss) *(.bss.*) *(COMMON) } :data
}
