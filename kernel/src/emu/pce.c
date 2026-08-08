/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* pce.c — PC Engine / TurboGrafx-16 machine around the HuC6280. See pce.h. */
#include "pce.h"

/* HuCards carry no magic string. Heuristic: the reset vector (logical $FFFE,
 * which with MPR7=0 is ROM offset $1FFE) points into the high logical ROM
 * region ($E000-$FFFF), and the entry byte is a plausible opcode. Combined with
 * the behavioural verdict (a mis-routed image simply won't program the VDC). */
int pce_is_pce(const uint8_t *img, uint32_t len){
    if (!img || len < 0x2000) return 0;
    uint32_t base = (len % 0x2000 == 512) ? 512 : 0;
    if (len - base < 0x2000) return 0;
    const uint8_t *r = img + base;
    uint16_t rv = r[0x1FFE] | (r[0x1FFF] << 8);
    if (rv < 0xE000) return 0;
    uint8_t entry = r[rv & 0x1FFF];
    if (entry == 0x00 || entry == 0xFF) return 0;   /* not a real opcode */
    return 1;
}

static uint8_t pce_read(cpu_huc6280_t *c, uint32_t phys){
    pce_t *p = (pce_t*)c->ctx; phys &= 0x1FFFFF; uint32_t page = phys >> 13;
    if (page <= 0x7F) return p->rom_size ? p->rom[phys % p->rom_size] : 0;
    if (page == 0xF8) return p->ram[phys & 0x1FFF];
    if (page == 0xF7) return p->bram[phys & 0x1FFF];
    if (page == 0xFF){
        uint32_t o = phys & 0x1FFF;
        if (o <= 0x0003){ if (o == 0){ p->vdc_status_reads++; return p->vblank ? 0x20 : 0x00; } return 0; }
        if (o == 0x1000) return 0x0F;                      /* joypad: no buttons, region/CD bits clear */
        if (o == 0x1402) return p->irq_disable;
        if (o == 0x1403){ return 0; }                      /* IRQ status / ack */
        return 0;
    }
    return 0;
}
static void pce_write(cpu_huc6280_t *c, uint32_t phys, uint8_t v){
    pce_t *p = (pce_t*)c->ctx; phys &= 0x1FFFFF; uint32_t page = phys >> 13;
    if (page == 0xF8){ p->ram[phys & 0x1FFF] = v; return; }
    if (page == 0xF7){ p->bram[phys & 0x1FFF] = v; return; }
    if (page == 0xFF){
        uint32_t o = phys & 0x1FFF;
        if (o <= 0x0003){ p->vdc_writes++;
            if (o == 0) p->vdc_addr = v & 0x1F;
            else if (o == 2) p->vdc_reg[p->vdc_addr] = (p->vdc_reg[p->vdc_addr] & 0xFF00) | v;
            else if (o == 3){ p->vdc_reg[p->vdc_addr] = (p->vdc_reg[p->vdc_addr] & 0x00FF) | (v << 8);
                if (p->vdc_addr == 5) p->display_on = (p->vdc_reg[5] & 0x00C0) ? 1 : 0; }
            return;
        }
        if (o >= 0x0400 && o <= 0x0407){ p->vce_writes++; return; }
        if (o == 0x1402){ p->irq_disable = v; return; }
        return;
    }
    /* ROM writes ignored (no SF2-style mapper modelled) */
}

int pce_load(pce_t *p, const uint8_t *img, uint32_t len){
    if (!img || len < 0x2000) return 0;
    for (unsigned i = 0; i < sizeof *p; i++) ((uint8_t*)p)[i] = 0;
    uint32_t base = (len % 0x2000 == 512) ? 512 : 0;
    uint32_t n = len - base; if (n > PCE_ROM_CAP) n = PCE_ROM_CAP;
    for (uint32_t i = 0; i < n; i++) p->rom[i] = img[base + i];
    p->rom_size = n;
    p->cpu.read = pce_read; p->cpu.write = pce_write; p->cpu.ctx = p;
    cpu_huc6280_reset(&p->cpu);
    return 1;
}

void pce_run(pce_t *p, uint32_t budget){
    const uint32_t per_line = 400;
    for (uint32_t i = 0; i < budget; i++){
        cpu_huc6280_step(&p->cpu);
        p->insn++;
        if ((i % per_line) == (per_line - 1)){
            p->v_counter++;
            if (p->v_counter == 240){
                p->vblank = 1;
                if ((p->vdc_reg[5] & 0x08) && !(p->irq_disable & 0x02) && !(p->cpu.p & HUC_I)){
                    cpu_huc6280_irq(&p->cpu); p->vblank_irqs++;
                }
            }
            if (p->v_counter > 262){ p->v_counter = 0; p->vblank = 0; }
        }
    }
}

int pce_is_running(const pce_t *p){
    /* Alive iff it programmed the VDC (a stream of ST/MMIO writes) and reached a
     * frame loop (VBlank IRQ taken OR status polled), with a low illegal rate. */
    int vdc    = (p->vdc_writes >= 8);
    int frame  = (p->vblank_irqs >= 1) || (p->vdc_status_reads >= 8);
    int clean  = (p->insn > 0) && (p->cpu.illegal * 20 < p->insn);   /* <5% illegal */
    return vdc && frame && clean;
}

int pce_selfcheck(void){
    static uint8_t img[0x2000];
    for (unsigned i = 0; i < sizeof img; i++) img[i] = 0;
    /* entry at logical $E000 == ROM offset 0 (MPR7=0 maps $E000-$FFFF to bank 0) */
    uint32_t p0 = 0;
    #define B(x) (img[p0++] = (uint8_t)(x))
    B(0x78);                    /* SEI                     */
    B(0xD4);                    /* CSH                     */
    B(0xD8);                    /* CLD                     */
    /* program the VDC control register (reg 5): VBlank IRQ + display enable */
    B(0x03); B(0x05);           /* ST0 #5   (select CR)    */
    B(0x13); B(0xC8);           /* ST1 #$C8 (CR lo: bg+spr display + VBlank IRQ) */
    B(0x23); B(0x00);           /* ST2 #$00 (CR hi)        */
    B(0x03); B(0x00);           /* ST0 #0                  */
    B(0x13); B(0x00);           /* ST1 #0                  */
    B(0x23); B(0x00);           /* ST2 #0                  */
    B(0x03); B(0x07);           /* ST0 #7 (BXR)            */
    B(0x13); B(0x00);           /* ST1 #0                  */
    B(0x23); B(0x00);           /* ST2 #0  (>=8 VDC writes)*/
    B(0x58);                    /* CLI                     */
    /* loop: BRA loop */
    uint32_t loop = p0;
    B(0x80);
    { int8_t off = (int8_t)((int32_t)loop - (int32_t)(p0 + 1)); B((uint8_t)off); }

    /* IRQ1 handler at logical $E100 == ROM offset $0100: RTI (ack via vblank) */
    img[0x0100] = 0x40;         /* RTI */

    /* vectors (logical $FFF8 IRQ1, $FFFE reset -> ROM $1FF8 / $1FFE) */
    img[0x1FF8] = 0x00; img[0x1FF9] = 0xE1;   /* IRQ1 -> $E100 */
    img[0x1FFE] = 0x00; img[0x1FFF] = 0xE0;   /* reset -> $E000 */

    static pce_t pc;
    if (!pce_is_pce(img, sizeof img)) return 0;
    if (!pce_load(&pc, img, sizeof img)) return 0;
    pce_run(&pc, 200000);   /* enough for the V-counter to reach VBlank (line 240) */
    return pce_is_running(&pc);
}
