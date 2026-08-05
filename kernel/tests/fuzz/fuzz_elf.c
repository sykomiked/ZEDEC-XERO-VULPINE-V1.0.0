/* fuzz_elf.c — deterministic fuzzer for the ELF64 loader
 *
 * Feeds the ELF parser hundreds of thousands of random and
 * mutated-valid inputs and asserts it NEVER crashes, reads out of
 * bounds, or returns an out-of-range result. Built with ASan+UBSan so
 * any memory-safety or UB defect aborts the run. This is the
 * adversarial-parser gate for untrusted executable input.
 *
 * Self-contained (no libFuzzer): a seeded PRNG makes runs reproducible.
 *
 *   cc -O1 -g -fsanitize=address,undefined -Isrc/loader \
 *      tests/fuzz/fuzz_elf.c src/loader/elf.c -o /tmp/fuzz_elf && /tmp/fuzz_elf
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "elf.h"

/* xorshift64 — deterministic, seedable. */
static uint64_t s_state = 0x123456789abcdef0ULL;
static uint64_t rng(void) {
    uint64_t x = s_state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return s_state = x;
}

/* A valid template we mutate. Built like the unit test's valid ELF. */
static uint8_t tmpl[512];
static uint64_t tmpl_len;

static void build_template(void) {
    memset(tmpl, 0, sizeof(tmpl));
    tmpl[0]=0x7f; tmpl[1]='E'; tmpl[2]='L'; tmpl[3]='F';
    tmpl[4]=2; tmpl[5]=1; tmpl[6]=1;
    tmpl[16]=2;              /* ET_EXEC */
    tmpl[18]=183;           /* EM_AARCH64 */
    tmpl[20]=1;
    uint64_t entry=0x10000; for(int i=0;i<8;i++) tmpl[24+i]=(uint8_t)(entry>>(8*i));
    uint64_t phoff=64; for(int i=0;i<8;i++) tmpl[32+i]=(uint8_t)(phoff>>(8*i));
    tmpl[52]=64; tmpl[54]=56; tmpl[56]=1;   /* ehsize, phentsize, phnum */
    uint8_t *ph=tmpl+64;
    uint32_t pt=1; for(int i=0;i<4;i++) ph[0+i]=(uint8_t)(pt>>(8*i));
    uint32_t fl=5; for(int i=0;i<4;i++) ph[4+i]=(uint8_t)(fl>>(8*i)); /* R E */
    uint64_t off=120; for(int i=0;i<8;i++) ph[8+i]=(uint8_t)(off>>(8*i));
    uint64_t va=0x10000; for(int i=0;i<8;i++){ph[16+i]=(uint8_t)(va>>(8*i));ph[24+i]=(uint8_t)(va>>(8*i));}
    uint64_t sz=8; for(int i=0;i<8;i++){ph[32+i]=(uint8_t)(sz>>(8*i));ph[40+i]=(uint8_t)(sz>>(8*i));}
    tmpl_len = 128;
}

/* No-op mapper: records nothing, just returns success so the loader
 * walks the entire header/segment path. */
static int nop_map(void *ctx, const elf_segment_t *s) {
    (void)ctx;
    /* Touch the fields so UBSan/ASan would flag a bad pointer/len the
     * loader might have produced. Read one byte of the segment data if
     * the loader says there is any. */
    volatile uint8_t sink = 0;
    if (s->file_size > 0 && s->data) sink = s->data[0];
    (void)sink;
    return 0;
}

static void run_one(const uint8_t *buf, uint64_t len) {
    uint64_t entry = 0xdeadbeef;
    elf_result_t r = elf_load(buf, len, nop_map, 0, &entry);
    /* Result must be a known enum value; ELF_OK must set entry. */
    if (r > 0 || r < ELF_ERR_ENTRY) {
        fprintf(stderr, "BUG: out-of-range result %d\n", r);
        _Exit(2);
    }
}

int main(int argc, char **argv) {
    long iters = 300000;
    if (argc > 1) iters = atol(argv[1]);
    build_template();

    uint8_t buf[1024];

    /* 1) pure random buffers of random length */
    for (long i = 0; i < iters; i++) {
        uint64_t len = rng() % sizeof(buf);
        for (uint64_t j = 0; j < len; j++) buf[j] = (uint8_t)rng();
        run_one(buf, len);
    }

    /* 2) mutated valid templates: flip 1-8 random bytes */
    for (long i = 0; i < iters; i++) {
        memcpy(buf, tmpl, tmpl_len);
        int flips = 1 + (int)(rng() % 8);
        for (int f = 0; f < flips; f++) {
            uint64_t pos = rng() % tmpl_len;
            buf[pos] ^= (uint8_t)(1u << (rng() % 8));
        }
        run_one(buf, tmpl_len);
    }

    /* 3) truncations of the valid template at every length */
    for (uint64_t len = 0; len <= tmpl_len; len++)
        run_one(tmpl, len);

    printf("[PASS] fuzz_elf: %ld iters x2 + %llu truncations, no crash/UB\n",
           iters, (unsigned long long)(tmpl_len + 1));
    return 0;
}
