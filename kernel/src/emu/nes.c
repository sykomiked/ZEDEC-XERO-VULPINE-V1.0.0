/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* nes.c — NES mapper-0 (NROM) machine. See nes.h. Integer-only, freestanding. */
#include "nes.h"

/* The bus ctx is the nes_t itself (cpu.ctx points at it). */
static uint8_t nes_read(cpu6502_t *c, uint16_t a){
    nes_t *n = (nes_t *)c->ctx;
    if (a < 0x2000) return n->ram[a & 0x07FF];
    if (a < 0x4000){
        uint16_t reg = 0x2000u + (a & 7u);
        if (reg == 0x2002){                       /* PPUSTATUS */
            n->ppu_status_reads++;
            /* Faithful vblank: return the current status, then CLEAR the vblank
             * bit (hardware clears it on read). nes_run SETS it once per frame.
             * This makes BOTH poll patterns terminate — "wait until set" exits
             * at the frame boundary, "wait until clear" exits on the next read. */
            uint8_t r = n->ppu_status;
            n->ppu_status &= (uint8_t)~0x80u;
            return r;
        }
        return 0;                                 /* other PPU regs read as 0 */
    }
    if (a < 0x4018){
        /* $4016/$4017 controller: report no buttons. */
        return 0;
    }
    if (a >= 0x8000) return n->prg[(a - 0x8000u) & n->prg_mask];
    return 0;                                     /* open bus */
}

static void nes_write(cpu6502_t *c, uint16_t a, uint8_t v){
    nes_t *n = (nes_t *)c->ctx;
    if (a < 0x2000){ n->ram[a & 0x07FF] = v; return; }
    if (a < 0x4000){
        uint16_t reg = 0x2000u + (a & 7u);
        n->ppu_reg_writes++;
        if (reg == 0x2000) n->ppu_ctrl = v;
        else if (reg == 0x2001) n->ppu_mask = v;
        return;
    }
    if (a < 0x4018){ n->apu_io_writes++; return; } /* APU + OAMDMA + controller strobe */
    /* $8000+ is ROM on NROM — writes ignored (no mapper registers). */
    (void)a;
}

int nes_is_ines(const uint8_t *img, uint32_t len){
    return (len >= 16 && img[0]==0x4E && img[1]==0x45 && img[2]==0x53 && img[3]==0x1A);
}

int nes_load_ines(nes_t *n, const uint8_t *img, uint32_t len){
    for (unsigned i = 0; i < sizeof *n; i++) ((uint8_t*)n)[i] = 0;
    if (!nes_is_ines(img, len)) return 0;
    uint32_t prg16k = img[4];                     /* PRG-ROM in 16KB units */
    uint32_t off = 16;
    /* If a trainer is present (flag6 bit2), PRG follows the 512-byte trainer. */
    if (img[6] & 0x04) off += 512;
    uint32_t prg = prg16k * 16384u;
    if (off + prg > len) prg = (len > off) ? (len - off) : 0;
    if (prg == 0) return 0;
    /* The 6502 sees a fixed 32KB window $8000-$FFFF. The reset/NMI/IRQ vectors
     * live in the LAST 16KB bank, which every mapper leaves mapped at $C000 on
     * power-on (NROM mirrors its single bank; MMC1/MMC3 fix the last bank there).
     * So: first 16KB -> $8000, LAST 16KB -> $C000. For <=16KB, mirror. This puts
     * the real reset vector at $FFFC for all PRG sizes, so init code runs even
     * without live bank switching. */
    if (prg <= 16384u){
        for (uint32_t i = 0; i < prg; i++){        /* mirror the single bank */
            n->prg[i] = img[off + i];
            n->prg[16384u + i] = img[off + i];
        }
    } else {
        for (uint32_t i = 0; i < 16384u; i++)      n->prg[i] = img[off + i];               /* first bank -> $8000 */
        for (uint32_t i = 0; i < 16384u; i++)      n->prg[16384u + i] = img[off + prg - 16384u + i]; /* LAST bank -> $C000 */
    }
    n->prg_mask = 0x7FFFu;
    n->cpu.read = nes_read; n->cpu.write = nes_write; n->cpu.ctx = n;
    n->cpu.jammed = 0; n->cpu.last_opcode = 0; n->cpu.illegal = 0;
    return 1;
}

void nes_run(nes_t *n, uint32_t budget, uint32_t nmi_period){
    cpu6502_reset(&n->cpu);                        /* PC <- [$FFFC] */
    if (nmi_period == 0) nmi_period = 2000;
    uint32_t i = 0;
    for (; i < budget && !n->cpu.jammed; i++){
        if (cpu6502_step(&n->cpu) == 0) break;
        /* Once per "frame": raise vblank, and deliver the vblank NMI if the game
         * enabled it (PPUCTRL bit7). Real games do per-frame work in the NMI
         * handler and/or poll vblank — this carries them into the main loop. */
        if ((i % nmi_period) == (nmi_period - 1)){
            n->ppu_status |= 0x80u;                /* vblank begins */
            if (n->ppu_ctrl & 0x80u){ cpu6502_nmi(&n->cpu); n->nmis_taken++; }
        }
    }
    n->insn = i;
}

/* Self-check: a hand-built minimal NROM image that sets up the stack, enables
 * NMI, programs PPUCTRL/PPUMASK, then polls vblank forever — the canonical NES
 * init pattern. Proves the machine runs a real NES program ON TARGET without an
 * external ROM. Returns 1 if the synthetic game comes up "running". */
int nes_selfcheck(void){
    static uint8_t img[16 + 16384];
    for (unsigned i = 0; i < sizeof img; i++) img[i] = 0;
    img[0]='N'; img[1]='E'; img[2]='S'; img[3]=0x1A; img[4]=1;   /* 16KB PRG */
    /* code at $C000 (16KB bank mirrors to $8000 and $C000): */
    static const uint8_t code[] = {
        0x78,             /* SEI            */
        0xA2,0xFF, 0x9A,  /* LDX #$FF; TXS  */
        0xA9,0x80, 0x8D,0x00,0x20,  /* LDA #$80; STA $2000 (enable NMI) */
        0xA9,0x1E, 0x8D,0x01,0x20,  /* LDA #$1E; STA $2001 (show bg+spr) */
        0x2C,0x02,0x20,   /* loop: BIT $2002  ($C00E) */
        0x10,0xFB,        /*       BPL loop (-5)      */
        0x4C,0x0E,0xC0    /*       JMP $C00E          */
    };
    for (unsigned i = 0; i < sizeof code; i++) img[16 + i] = code[i];
    img[16 + 0x16] = 0x40;                       /* RTI at $C016 (NMI handler) */
    unsigned v = 16 + 0x3FFA;                    /* vectors in last 6 PRG bytes */
    img[v+0]=0x16; img[v+1]=0xC0;                /* NMI   = $C016 */
    img[v+2]=0x00; img[v+3]=0xC0;                /* RESET = $C000 */
    img[v+4]=0x16; img[v+5]=0xC0;                /* IRQ   = $C016 */
    static nes_t n;
    if (!nes_load_ines(&n, img, sizeof img)) return 0;
    nes_run(&n, 20000u, 2000u);
    return nes_is_running(&n) ? 1 : 0;
}

int nes_is_running(const nes_t *n){
    /* Behavioural signature of a live NES game: clean 6502 execution (near-zero
     * illegal) that CONFIGURES the PPU and drives frames — by polling vblank,
     * running on NMIs, or heavily programming the PPU register file. Random data
     * never touches $2000-$2007 in this structured, low-illegal way. */
    if (n->insn == 0) return 0;
    uint32_t ill_permille = (uint32_t)((uint64_t)n->cpu.illegal * 1000u / n->insn);
    if (ill_permille >= 100u) return 0;            /* wandering through data     */
    if (n->ppu_reg_writes < 2) return 0;           /* never programmed the PPU   */
    if (n->ppu_status_reads >= 2) return 1;        /* polled vblank              */
    if (n->nmis_taken >= 1)      return 1;         /* runs on the vblank NMI     */
    if (n->ppu_reg_writes >= 50) return 1;         /* heavy PPU programming      */
    return 0;
}
