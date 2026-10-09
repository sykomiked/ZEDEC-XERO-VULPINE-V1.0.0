/* test_elf.c — host unit + adversarial tests for the ELF64 loader
 *
 * Includes negative tests (the "adversarial parser" gate): truncated
 * headers, bad magic/class/machine/type, W^X segments, out-of-window
 * vaddrs, file ranges past EOF, and integer-overflow offsets must all
 * be rejected without a map callback ever touching bad memory.
 *
 * Build/run:
 *   gcc -std=c11 -Wall -Wextra -Isrc/loader \
 *       src/loader/test_elf.c src/loader/elf.c -o /tmp/test_elf && /tmp/test_elf
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "elf.h"

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("[FAIL] %s\n", msg);                                                            \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("[PASS] %s\n", msg);                                                            \
        }                                                                                          \
    } while (0)

/* Collected segments from a load, for assertions. */
static elf_segment_t g_segs[16];
static int g_nseg;
static int collect(void *ctx, const elf_segment_t *s)
{
    (void) ctx;
    if (g_nseg < 16) g_segs[g_nseg++] = *s;
    return 0;
}
static int reject_all(void *ctx, const elf_segment_t *s)
{
    (void) ctx;
    (void) s;
    return 1;
}

/* Build a minimal valid ELF64 AArch64 ET_EXEC with one RX PT_LOAD at
 * 0x10000 and (optionally) a second segment we control. Returns length. */
#define EH 64
#define PH 56
static uint64_t build_elf(uint8_t *buf, uint64_t vaddr, uint32_t flags, uint32_t filesz,
                          uint32_t memsz, uint64_t entry, int nph)
{
    memset(buf, 0, 512);
    /* e_ident */
    buf[0] = 0x7f;
    buf[1] = 'E';
    buf[2] = 'L';
    buf[3] = 'F';
    buf[4] = 2; /* ELFCLASS64 */
    buf[5] = 1; /* little-endian */
    buf[6] = 1; /* version */
    /* e_type = ET_EXEC (2) */
    buf[16] = 2;
    buf[17] = 0;
    /* e_machine = EM_AARCH64 (183) */
    buf[18] = 183;
    buf[19] = 0;
    /* e_version */
    buf[20] = 1;
    /* e_entry (8 bytes @24) */
    for (int i = 0; i < 8; i++) buf[24 + i] = (uint8_t) (entry >> (8 * i));
    /* e_phoff = 64 (8 bytes @32) */
    uint64_t phoff = EH;
    for (int i = 0; i < 8; i++) buf[32 + i] = (uint8_t) (phoff >> (8 * i));
    /* e_ehsize @52, e_phentsize @54, e_phnum @56 */
    buf[52] = EH;
    buf[53] = 0;
    buf[54] = PH;
    buf[55] = 0;
    buf[56] = (uint8_t) nph;
    buf[57] = 0;

    /* program header 0 @ 64 */
    uint8_t *ph = buf + EH;
    uint32_t p_type = 1 /*PT_LOAD*/;
    for (int i = 0; i < 4; i++) ph[0 + i] = (uint8_t) (p_type >> (8 * i));
    for (int i = 0; i < 4; i++) ph[4 + i] = (uint8_t) (flags >> (8 * i));
    uint64_t p_offset = EH + (uint64_t) PH * nph; /* data after all phdrs */
    for (int i = 0; i < 8; i++) ph[8 + i] = (uint8_t) (p_offset >> (8 * i));
    for (int i = 0; i < 8; i++) ph[16 + i] = (uint8_t) (vaddr >> (8 * i)); /* p_vaddr */
    for (int i = 0; i < 8; i++) ph[24 + i] = (uint8_t) (vaddr >> (8 * i)); /* p_paddr */
    for (int i = 0; i < 8; i++) ph[32 + i] = (uint8_t) ((uint64_t) filesz >> (8 * i));
    for (int i = 0; i < 8; i++) ph[40 + i] = (uint8_t) ((uint64_t) memsz >> (8 * i));
    return p_offset + filesz;
}

int main(void)
{
    uint8_t buf[512];
    uint64_t entry;
    elf_result_t r;

    printf("=== ELF64 loader tests ===\n");

    /* valid: one RX segment at 0x10000, entry at start */
    uint64_t len = build_elf(buf, 0x10000, PF_R | PF_X, 8, 8, 0x10000, 1);
    g_nseg = 0;
    r = elf_load(buf, len, collect, 0, &entry);
    CHECK(r == ELF_OK, "valid RX exec loads");
    CHECK(entry == 0x10000, "entry point correct");
    CHECK(g_nseg == 1 && g_segs[0].executable && !g_segs[0].writable,
          "segment is RX (not writable)");

    /* bad magic */
    build_elf(buf, 0x10000, PF_R | PF_X, 8, 8, 0x10000, 1);
    buf[1] = 'X';
    CHECK(elf_load(buf, len, collect, 0, &entry) == ELF_ERR_MAGIC, "bad magic rejected");

    /* not ELF64 */
    build_elf(buf, 0x10000, PF_R | PF_X, 8, 8, 0x10000, 1);
    buf[4] = 1; /* ELFCLASS32 */
    CHECK(elf_load(buf, len, collect, 0, &entry) == ELF_ERR_CLASS, "32-bit ELF rejected");

    /* wrong machine */
    build_elf(buf, 0x10000, PF_R | PF_X, 8, 8, 0x10000, 1);
    buf[18] = 0x3e; /* EM_X86_64 */
    CHECK(elf_load(buf, len, collect, 0, &entry) == ELF_ERR_MACHINE, "non-AArch64 rejected");

    /* not ET_EXEC (ET_DYN=3) */
    build_elf(buf, 0x10000, PF_R | PF_X, 8, 8, 0x10000, 1);
    buf[16] = 3;
    CHECK(elf_load(buf, len, collect, 0, &entry) == ELF_ERR_TYPE, "non-static (ET_DYN) rejected");

    /* W^X violation: RWX segment */
    len = build_elf(buf, 0x10000, PF_R | PF_W | PF_X, 8, 8, 0x10000, 1);
    CHECK(elf_load(buf, len, collect, 0, &entry) == ELF_ERR_WX, "W+X segment rejected (W^X)");

    /* out of window: vaddr below ELF_VA_MIN */
    len = build_elf(buf, 0x100, PF_R | PF_X, 8, 8, 0x100, 1);
    CHECK(elf_load(buf, len, collect, 0, &entry) == ELF_ERR_RANGE, "vaddr below window rejected");

    /* out of window: vaddr + memsz above ELF_VA_MAX */
    len = build_elf(buf, 0x1FF000, PF_R | PF_X, 8, 0x8000, 0x1FF000, 1);
    CHECK(elf_load(buf, len, collect, 0, &entry) == ELF_ERR_RANGE,
          "segment past window ceiling rejected");

    /* filesz points past EOF */
    len = build_elf(buf, 0x10000, PF_R | PF_X, 8, 8, 0x10000, 1);
    /* rewrite p_filesz to something huge */
    {
        uint8_t *ph = buf + EH;
        uint64_t big = 0x100000;
        for (int i = 0; i < 8; i++) ph[32 + i] = (uint8_t) (big >> (8 * i));
    }
    CHECK(elf_load(buf, len, collect, 0, &entry) == ELF_ERR_SHORT, "filesz past EOF rejected");

    /* integer-overflow p_offset + p_filesz */
    len = build_elf(buf, 0x10000, PF_R | PF_X, 8, 8, 0x10000, 1);
    {
        uint8_t *ph = buf + EH;
        uint64_t huge = 0xFFFFFFFFFFFFFF00ULL;
        for (int i = 0; i < 8; i++) ph[8 + i] = (uint8_t) (huge >> (8 * i));
    } /* p_offset */
    CHECK(elf_load(buf, len, collect, 0, &entry) == ELF_ERR_SHORT, "overflowing p_offset rejected");

    /* truncated image (smaller than ehdr) */
    CHECK(elf_load(buf, 10, collect, 0, &entry) == ELF_ERR_SHORT, "truncated image rejected");

    /* entry not inside any segment */
    len = build_elf(buf, 0x10000, PF_R | PF_X, 8, 8, 0x1F0000, 1);
    CHECK(elf_load(buf, len, collect, 0, &entry) == ELF_ERR_ENTRY,
          "entry outside segments rejected");

    /* mapper rejects a segment -> ELF_ERR_MAP */
    len = build_elf(buf, 0x10000, PF_R | PF_X, 8, 8, 0x10000, 1);
    CHECK(elf_load(buf, len, reject_all, 0, &entry) == ELF_ERR_MAP, "mapper rejection propagates");

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
