/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* gb.c — Game Boy / GBC machine around the LR35902. See gb.h. */
#include "gb.h"

/* The 48-byte Nintendo logo every real cartridge stores at $0104 — the boot ROM
 * verifies it, so it is a near-perfect "is this a GB ROM" signature. We match a
 * distinctive prefix (enough to reject non-GB data with negligible collision). */
static const uint8_t GB_LOGO[16] = {
    0xCE,0xED,0x66,0x66,0xCC,0x0D,0x00,0x0B,0x03,0x73,0x00,0x83,0x00,0x0C,0x00,0x0D
};

int gb_is_gb(const uint8_t *img, uint32_t len){
    if (!img || len < 0x150) return 0;
    for (int i = 0; i < 16; i++) if (img[0x104 + i] != GB_LOGO[i]) return 0;
    return 1;
}

static uint8_t gb_read(cpu_lr35902_t *c, uint16_t a){
    gb_t *g = (gb_t*)c->ctx;
    if (a < 0x4000) return g->rom_size ? g->rom[a % g->rom_size] : 0;         /* bank 0 */
    if (a < 0x8000){                                                          /* banked */
        uint32_t off = g->rom_bank * 0x4000u + (a - 0x4000u);
        return g->rom_size ? g->rom[off % g->rom_size] : 0;
    }
    if (a < 0xA000) return g->vram[a - 0x8000];
    if (a < 0xC000) return g->ram_enable ? g->cart_ram[(a - 0xA000) & 0x7FFF] : 0xFF;
    if (a < 0xE000) return g->wram[a - 0xC000];
    if (a < 0xFE00) return g->wram[a - 0xE000];                              /* echo   */
    if (a < 0xFEA0) return g->oam[a - 0xFE00];
    if (a < 0xFF00) return 0;
    if (a < 0xFF80){                                                          /* I/O    */
        if (a == 0xFF44){ g->ly_reads++; return g->ly; }                     /* LY     */
        return g->io[a - 0xFF00];
    }
    if (a < 0xFFFF) return g->hram[a - 0xFF80];
    return g->ie;
}

static void gb_write(cpu_lr35902_t *c, uint16_t a, uint8_t v){
    gb_t *g = (gb_t*)c->ctx;
    if (a < 0x2000){ g->ram_enable = ((v & 0x0F) == 0x0A); return; }          /* RAM enable */
    if (a < 0x4000){ uint32_t b = v & 0x7F; if (b == 0) b = 1; g->rom_bank = b; return; } /* ROM bank */
    if (a < 0x6000) return;                                                   /* RAM bank / hi */
    if (a < 0x8000) return;                                                   /* banking mode  */
    if (a < 0xA000){ g->vram[a - 0x8000] = v; return; }
    if (a < 0xC000){ if (g->ram_enable) g->cart_ram[(a - 0xA000) & 0x7FFF] = v; return; }
    if (a < 0xE000){ g->wram[a - 0xC000] = v; return; }
    if (a < 0xFE00){ g->wram[a - 0xE000] = v; return; }
    if (a < 0xFEA0){ g->oam[a - 0xFE00] = v; return; }
    if (a < 0xFF00) return;
    if (a < 0xFF80){                                                          /* I/O    */
        g->io_writes++;
        if (a == 0xFF40 && (v & 0x80)) g->lcd_on = 1;                         /* LCDC on */
        if (a == 0xFF44){ g->io[0x44] = 0; return; }                          /* LY reset */
        g->io[a - 0xFF00] = v;
        return;
    }
    if (a < 0xFFFF){ g->hram[a - 0xFF80] = v; return; }
    g->ie = v;
}

int gb_load(gb_t *g, const uint8_t *img, uint32_t len){
    if (!img || len < 0x150) return 0;
    for (unsigned i = 0; i < sizeof *g; i++) ((uint8_t*)g)[i] = 0;
    uint32_t n = len; if (n > GB_ROM_CAP) n = GB_ROM_CAP;
    for (uint32_t i = 0; i < n; i++) g->rom[i] = img[i];
    g->rom_size = n;
    g->rom_bank = 1;
    g->cpu.read = gb_read; g->cpu.write = gb_write; g->cpu.ctx = g;
    cpu_lr_reset(&g->cpu);
    return 1;
}

void gb_run(gb_t *g, uint32_t budget){
    /* ~114 CPU steps per scanline; LY 0..153, VBlank begins at LY==144. Drive it
     * off the instruction count (behavioural, not cycle-accurate). */
    const uint32_t per_line = 114;
    for (uint32_t i = 0; i < budget; i++){
        cpu_lr_step(&g->cpu);
        g->insn++;
        if ((i % per_line) == (per_line - 1)){
            g->ly++;
            if (g->ly == 144){                                               /* enter VBlank */
                g->io[0x0F] |= 0x01;                                          /* IF: VBlank   */
                if ((g->ie & 0x01) && g->cpu.ime){
                    cpu_lr_interrupt(&g->cpu, 0x0040);
                    g->io[0x0F] &= ~0x01;
                    g->vblanks++;
                }
            }
            if (g->ly > 153) g->ly = 0;
        }
    }
}

int gb_is_running(const gb_t *g){
    /* Alive iff it turned the LCD on, drove the I/O file, and reached a VBlank-
     * driven loop (took a VBlank interrupt OR is polling LY), with a low rate of
     * undefined opcodes. Random data does not turn the LCD on and take VBlanks. */
    int lcd    = g->lcd_on;
    int io_ok  = (g->io_writes >= 4);
    int frameloop = (g->vblanks >= 1) || (g->ly_reads >= 8);
    int clean  = (g->insn > 0) && (g->cpu.illegal * 20 < g->insn);   /* <5% illegal */
    return lcd && io_ok && frameloop && clean;
}

int gb_selfcheck(void){
    static uint8_t img[0x8000];
    for (unsigned i = 0; i < sizeof img; i++) img[i] = 0;
    /* Nintendo logo so gb_is_gb + this machine agree it's a cartridge. */
    for (int i = 0; i < 16; i++) img[0x104 + i] = GB_LOGO[i];

    /* entry: $0100 NOP; JP $0150 */
    img[0x100] = 0x00; img[0x101] = 0xC3; img[0x102] = 0x50; img[0x103] = 0x01;
    /* VBlank vector $0040: push none — LD A,($FF00+...) style bump a HRAM counter, RETI */
    uint32_t v = 0x0040;
    img[v++] = 0xF0; img[v++] = 0x80;      /* LDH A,($80)  */
    img[v++] = 0x3C;                        /* INC A        */
    img[v++] = 0xE0; img[v++] = 0x80;      /* LDH ($80),A  */
    img[v++] = 0xD9;                        /* RETI         */
    /* main at $0150 */
    uint32_t p = 0x0150;
    #define B(x) (img[p++] = (uint8_t)(x))
    B(0x31); B(0xFE); B(0xFF);              /* LD SP,$FFFE          */
    B(0x3E); B(0x91);                       /* LD A,$91             */
    B(0xE0); B(0x40);                       /* LDH ($40),A  LCDC on */
    B(0x3E); B(0xE4);                       /* LD A,$E4             */
    B(0xE0); B(0x47);                       /* LDH ($47),A  BGP     */
    B(0x3E); B(0x00);                       /* LD A,$00             */
    B(0xE0); B(0x42);                       /* LDH ($42),A  SCY     */
    B(0xE0); B(0x43);                       /* LDH ($43),A  SCX     */
    B(0x3E); B(0x01);                       /* LD A,$01             */
    B(0xE0); B(0x0F);                       /* LDH ($0F),A  IF      */
    B(0xEA); B(0xFF); B(0xFF);              /* LD ($FFFF),A IE=1    */
    B(0xFB);                                /* EI                   */
    /* loop: LDH A,($44) ; JR loop  (poll LY, VBlank ISR runs in between) */
    uint32_t loop = p;
    B(0xF0); B(0x44);                       /* LDH A,($44)          */
    B(0x18);                                /* JR                   */
    { int8_t off = (int8_t)((int32_t)loop - (int32_t)(p + 1)); B((uint8_t)off); }
    (void)loop;

    static gb_t g;
    if (!gb_is_gb(img, sizeof img)) return 0;
    if (!gb_load(&g, img, sizeof img)) return 0;
    gb_run(&g, 20000);
    return gb_is_running(&g);
}
