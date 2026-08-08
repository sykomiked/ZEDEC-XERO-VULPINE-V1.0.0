/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* snes.c — LoROM SNES machine around the 65816. See snes.h. */
#include "snes.h"

/* Two mappings share the same WRAM ($7E-$7F), the low-RAM mirror ($0000-$1FFF in
 * banks $00-$3F/$80-$BF), and the register file ($2100-$21FF, $4200-$44FF). They
 * differ only in how ROM is placed:
 *   LoROM: banks $00-$7D/$80-$FF, offset $8000-$FFFF hold 32KB ROM pages
 *          -> ROM offset = ((bank & 0x7F) << 15) | (off & 0x7FFF).
 *   HiROM: banks $C0-$FF (and $40-$7D) map the FULL 64KB linearly; the system
 *          banks' $8000-$FFFF window is the upper half of the same 64KB bank
 *          -> ROM offset = ((bank & 0x3F) << 16) | off. Header + vectors live at
 *          $FFC0/$FFFC (ROM offset 0xFFC0/0xFFFC), not $7FC0/$7FFC.
 * Returns 1 and sets *ro if the address is ROM, else 0. */
static int rom_offset(const snes_t *s, uint8_t bank, uint16_t off, uint32_t *ro){
    if (s->hirom){
        if (bank >= 0xC0){ *ro = ((uint32_t)(bank - 0xC0) << 16) | off; return 1; }
        if (bank >= 0x40 && bank <= 0x7D){ *ro = ((uint32_t)(bank - 0x40) << 16) | off; return 1; }
        if (off >= 0x8000){ *ro = ((uint32_t)(bank & 0x3F) << 16) | off; return 1; }
        return 0;
    }
    if (off >= 0x8000 || bank >= 0xC0){ *ro = ((uint32_t)(bank & 0x7F) << 15) | (off & 0x7FFF); return 1; }
    return 0;
}

/* Minimal general-purpose DMA. Writing $420B (MDMAEN) kicks the enabled channels.
 * Each channel transfers DAS bytes from the A-bus (24-bit source) to the B-bus
 * register ($2100 + BBAD), stepping the source per the mode/increment bits and
 * cycling the B-bus register through the transfer-mode pattern. HDMA + timing are
 * out of scope; this is enough to run WRAM clears and VRAM uploads so games get
 * past init. Bounded by a guard so a bad descriptor cannot hang the run. */
static void snes_run_dma(snes_t *s, uint8_t enable){
    static const uint8_t pat[8][4]    = {{0,0,0,0},{0,1,0,1},{0,0,0,0},{0,0,1,1},
                                         {0,1,2,3},{0,1,0,1},{0,0,0,0},{0,0,1,1}};
    static const uint8_t patlen[8]    = {1,2,1,2,4,2,1,2};
    for (int ch = 0; ch < 8; ch++){
        if (!(enable & (1u << ch))) continue;
        uint8_t *d = &s->dma[ch * 0x10];
        uint8_t  dmap = d[0], bbad = d[1];
        uint32_t a1 = (uint32_t)d[2] | ((uint32_t)d[3] << 8) | ((uint32_t)d[4] << 16);
        uint32_t das = (uint32_t)d[5] | ((uint32_t)d[6] << 8); if (das == 0) das = 0x10000;
        int inc  = (dmap & 0x08) ? 0 : ((dmap & 0x10) ? -1 : 1);   /* fixed / dec / inc */
        int mode = dmap & 0x07;
        uint32_t guard = 0; int pi = 0;
        while (das > 0 && guard < 0x20000){
            uint8_t v = s->cpu.read(&s->cpu, a1 & 0xFFFFFF);
            uint16_t target = 0x2100 | ((bbad + pat[mode][pi]) & 0xFF);
            s->cpu.write(&s->cpu, (uint32_t)target, v);          /* B-bus -> $21xx */
            a1 = (a1 & 0xFF0000) | ((a1 + inc) & 0xFFFF);
            pi = (pi + 1) % patlen[mode];
            das--; guard++;
        }
        d[2] = a1 & 0xFF; d[3] = (a1 >> 8) & 0xFF; d[5] = 0; d[6] = 0;  /* write-back */
        s->dma_runs++;
    }
}

static uint8_t snes_read(cpu65816_t *c, uint32_t addr){
    snes_t *s = (snes_t*)c->ctx;
    uint8_t bank = (addr >> 16) & 0xFF;
    uint16_t off = addr & 0xFFFF;

    if (bank == 0x7E || bank == 0x7F)
        return s->wram[((bank - 0x7E) << 16) | off];

    int sys = (bank <= 0x3F) || (bank >= 0x80 && bank <= 0xBF);
    if (sys && off < 0x8000){
        if (off < 0x2000) return s->wram[off];                 /* low RAM mirror  */
        if (off >= 0x2140 && off <= 0x2143){ s->apu_reads++; return s->apu[off - 0x2140]; } /* APU stub */
        if (off >= 0x2100 && off <= 0x21FF) return 0;          /* other PPU regs  */
        if (off == 0x4210){ uint8_t v = (s->vblank ? 0x80 : 0) | 0x02; s->vblank = 0; return v; } /* RDNMI */
        if (off == 0x4212) return s->vblank ? 0x80 : 0;        /* HVBJOY vblank   */
        if (off >= 0x4200 && off <= 0x44FF) return 0;          /* CPU/DMA regs    */
        return 0;
    }
    uint32_t o;
    if (rom_offset(s, bank, off, &o)) return s->rom_size ? s->rom[o % s->rom_size] : 0;
    return 0;
}

static void snes_write(cpu65816_t *c, uint32_t addr, uint8_t val){
    snes_t *s = (snes_t*)c->ctx;
    uint8_t bank = (addr >> 16) & 0xFF;
    uint16_t off = addr & 0xFFFF;

    if (bank == 0x7E || bank == 0x7F){ s->wram[((bank - 0x7E) << 16) | off] = val; return; }

    int sys = (bank <= 0x3F) || (bank >= 0x80 && bank <= 0xBF);
    if (sys && off < 0x8000){
        if (off < 0x2000){ s->wram[off] = val; return; }       /* low RAM mirror  */
        if (off >= 0x2100 && off <= 0x213F){                   /* PPU registers   */
            s->ppu_writes++;
            if (off == 0x2100) s->inidisp = val;
            return;
        }
        if (off >= 0x2140 && off <= 0x2143){ s->apu[off - 0x2140] = val; return; } /* APU echo */
        if (off >= 0x2144 && off <= 0x21FF) return;            /* other APU ports */
        if (off >= 0x4200 && off <= 0x421F){                   /* CPU registers   */
            s->cpu_reg_writes++;
            if (off == 0x4200){ s->nmitimen = val; s->nmi_enabled = (val >> 7) & 1; }
            if (off == 0x420B){ snes_run_dma(s, val); }        /* MDMAEN: kick DMA */
            return;
        }
        if (off >= 0x4300 && off <= 0x437F){ s->dma[off - 0x4300] = val; return; } /* DMA chan */
        if (off >= 0x4380 && off <= 0x44FF) return;            /* other regs      */
        return;
    }
    /* writes to ROM space (and HiROM full banks) are ignored */
}

/* A header (at $7FC0 for LoROM, $FFC0 for HiROM) is good if its checksum and
 * complement (offset +$1C / +$1E) XOR to $FFFF. */
static int hdr_good(const uint8_t *h, uint32_t hbase){
    uint16_t comp  = h[hbase + 0x1C] | (h[hbase + 0x1D] << 8);
    uint16_t chk   = h[hbase + 0x1E] | (h[hbase + 0x1F] << 8);
    return ((uint16_t)(comp ^ chk) == 0xFFFF) && ((comp | chk) != 0);
}

int snes_detect(const uint8_t *img, uint32_t len){
    if (!img || len < 0x8000) return 0;
    uint32_t base = (len % 0x8000 == 512) ? 512 : 0;           /* copier header   */
    if (len - base < 0x8000) return 0;
    const uint8_t *h = img + base;
    int lo = hdr_good(h, 0x7FC0);
    int hi = (len - base >= 0x10000) ? hdr_good(h, 0xFFC0) : 0;
    if (lo && !hi) return 1;
    if (hi && !lo) return 2;
    if (lo && hi)  return (h[0x7FD5] & 1) ? 2 : 1;             /* mode byte breaks tie */
    /* No valid checksum: use the map-mode byte + a plausible reset vector. */
    if (len - base >= 0x10000){
        uint16_t rhi = h[0xFFFC] | (h[0xFFFD] << 8);
        if ((h[0xFFD5] & 1) && rhi >= 0x8000) return 2;
    }
    uint16_t rlo = h[0x7FFC] | (h[0x7FFD] << 8);
    if (rlo >= 0x8000) return 1;
    return 0;
}

int snes_is_lorom(const uint8_t *img, uint32_t len){ return snes_detect(img, len) == 1; }
int snes_is_snes (const uint8_t *img, uint32_t len){ return snes_detect(img, len) != 0; }

int snes_load(snes_t *s, const uint8_t *img, uint32_t len){
    if (!img || len < 0x8000) return 0;
    int mapper = snes_detect(img, len);
    for (unsigned i = 0; i < sizeof *s; i++) ((uint8_t*)s)[i] = 0;
    s->hirom = (mapper == 2) ? 1 : 0;
    uint32_t base = (len % 0x8000 == 512) ? 512 : 0;
    uint32_t n = len - base;
    if (n > SNES_ROM_CAP) n = SNES_ROM_CAP;
    for (uint32_t i = 0; i < n; i++) s->rom[i] = img[base + i];
    s->rom_size = n;
    s->apu[0] = 0xAA; s->apu[1] = 0xBB;   /* SPC700 IPL "ready" signal */
    s->cpu.read = snes_read; s->cpu.write = snes_write; s->cpu.ctx = s;
    cpu65816_reset(&s->cpu);
    return 1;
}

void snes_run(snes_t *s, uint32_t budget, uint32_t nmi_period){
    if (nmi_period == 0) nmi_period = 2000;
    for (uint32_t i = 0; i < budget; i++){
        cpu65816_step(&s->cpu);
        s->insn++;
        if ((i % nmi_period) == (nmi_period - 1)){
            s->vblank = 1;
            if (s->nmi_enabled){ s->cpu.stopped = 0; cpu65816_nmi(&s->cpu); s->nmis_taken++; }
        }
    }
    /* Settle probe: a real game that finished init is now confined to a tight
     * loop — an NMI-wait (WAI/BRA), an APU handshake, or a poll — all a few
     * hundred bytes wide in one bank. Wandering "code" (random data executed as
     * opcodes) never settles: its PC sprays across the whole bank. This is the
     * coherence discriminator the 65816 lacks in illegal-opcode form. */
    uint16_t pmin = 0xFFFF, pmax = 0; uint8_t bank = s->cpu.pbr; int same = 1;
    for (int i = 0; i < 1024; i++){
        if (s->cpu.pbr != bank) same = 0;
        uint16_t pc = s->cpu.pc;
        if (pc < pmin) pmin = pc;
        if (pc > pmax) pmax = pc;
        cpu65816_step(&s->cpu);
        if (s->nmi_enabled && (i % 200) == 199){ s->vblank = 1; s->cpu.stopped = 0; cpu65816_nmi(&s->cpu); }
    }
    s->settled = same && ((uint16_t)(pmax - pmin) < 2048);
}

int snes_is_running(const snes_t *s){
    /* Alive iff the game entered native mode, drove the CPU register file, and is
     * demonstrably executing its real console setup — EITHER it reached its NMI
     * loop (enabled + took a vblank NMI) OR it is heavily configuring/uploading to
     * the PPU (screen setup / VRAM upload). Same behavioural spirit as the NES
     * verdict: real init hammers the PPU; random data does not. Low illegal rate. */
    int native   = (s->cpu.e == 0);
    int reg_ok   = (s->cpu_reg_writes >= 1);
    int clean    = (s->insn > 0) && (s->cpu.illegal * 20 < s->insn);   /* <5% illegal */
    /* Three strong, hard-to-fake signatures of a real game executing its boot:
     *  - it reached its NMI loop (enabled + took a vblank NMI), or
     *  - it is running the SPC700 APU handshake (thousands of $2140-$2143 reads —
     *    a tight, address-specific loop random data does not sustain), or
     *  - it is doing heavy PPU/VRAM setup (hundreds of $2100-$213F writes).
     * A modest scattering of PPU writes alone is NOT enough (random code can hit
     * a few by luck), which is why the bar is high on each axis. */
    /* Two HIGH-PRECISION hardware protocols random data will not sustain inside a
     * settled loop: reaching the vblank NMI loop, or running the SPC700 APU
     * handshake (a tight, address-specific read loop). PPU-write COUNT is
     * deliberately NOT a sufficient signal on its own — random STAs land in
     * $2100-$213F often enough over a long run to reach any count, so it cannot
     * separate a real VRAM upload from noise. Precision beats recall for a
     * RUNNING claim: some genuine PPU-only games are reported as not-verified
     * rather than risk calling noise a game. */
    int nmi_loop = (s->nmi_enabled && s->nmis_taken >= 1);
    int apu_hs   = (s->apu_reads >= 100);
    return native && reg_ok && clean && s->settled && (nmi_loop || apu_hs);
}

/* ---- embedded minimal LoROM for the self-check ----------------------------- */
int snes_selfcheck(void){
    static uint8_t img[0x8000];
    for (unsigned i = 0; i < sizeof img; i++) img[i] = 0;

    /* main program at ROM offset 0 (maps to bank $00:$8000) */
    uint32_t p = 0;
    #define B(x) (img[p++] = (uint8_t)(x))
    B(0x78);                    /* SEI                       */
    B(0x18);                    /* CLC                       */
    B(0xFB);                    /* XCE  -> native            */
    B(0xC2); B(0x30);           /* REP #$30 (16-bit A,X,Y)   */
    B(0xA2); B(0xFF); B(0x1F);  /* LDX #$1FFF                */
    B(0x9A);                    /* TXS                       */
    B(0xE2); B(0x20);           /* SEP #$20 (8-bit A)        */
    B(0xA9); B(0x8F);           /* LDA #$8F                  */
    B(0x8D); B(0x00); B(0x21);  /* STA $2100 (force blank)   */
    B(0xA9); B(0x0F);           /* LDA #$0F                  */
    B(0x8D); B(0x2C); B(0x21);  /* STA $212C (enable layers) */
    B(0xA9); B(0x81);           /* LDA #$81                  */
    B(0x8D); B(0x00); B(0x42);  /* STA $4200 (enable NMI)    */
    /* loop: WAI ; BRA loop  (wait for NMI)                  */
    uint32_t loop = p;
    B(0xCB);                    /* WAI                       */
    B(0x80); B(0xFE);           /* BRA loop (-2)             */
    (void)loop;

    /* NMI handler at ROM offset $0100 (bank $00:$8100): bump a WRAM counter, RTI */
    uint32_t h = 0x0100; uint32_t q = h;
    #define H(x) (img[q++] = (uint8_t)(x))
    H(0xE2); H(0x20);           /* SEP #$20 (8-bit A)        */
    H(0xAF); H(0x00); H(0x00); H(0x7E); /* LDA $7E0000        */
    H(0x1A);                    /* INC A                     */
    H(0x8F); H(0x00); H(0x00); H(0x7E); /* STA $7E0000        */
    H(0x40);                    /* RTI                       */

    /* vectors (LoROM: $7FEA native NMI, $7FFC reset) */
    img[0x7FEA] = 0x00; img[0x7FEB] = 0x81;   /* native NMI -> $8100 */
    img[0x7FFC] = 0x00; img[0x7FFD] = 0x80;   /* reset      -> $8000 */

    static snes_t s;
    if (!snes_load(&s, img, sizeof img)) return 0;
    snes_run(&s, 20000, 1500);

    /* the NMI handler must have run (counter advanced) and the verdict must hold */
    uint8_t counter = s.wram[0];
    return snes_is_running(&s) && counter >= 1;
}
