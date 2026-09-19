/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* sms.c — Sega Master System / Game Gear machine. See sms.h. Integer-only. */
#include "sms.h"

/* ---- memory bus (Sega mapper) ---- */
static uint8_t sms_rd(cpu_z80_t *c, uint16_t a){
    sms_t *s = (sms_t *)c->ctx;
    if (a < 0x0400) return s->rom[a];                       /* fixed bank-0 first 1KB */
    if (a < 0x4000) return s->rom[((uint32_t)s->page[0] * 0x4000u + a) % s->rom_size];
    if (a < 0x8000) return s->rom[((uint32_t)s->page[1] * 0x4000u + (a - 0x4000u)) % s->rom_size];
    if (a < 0xC000) return s->rom[((uint32_t)s->page[2] * 0x4000u + (a - 0x8000u)) % s->rom_size];
    return s->ram[a & 0x1FFF];                              /* $C000-$FFFF -> 8KB, mirrored */
}
static void sms_wr(cpu_z80_t *c, uint16_t a, uint8_t v){
    sms_t *s = (sms_t *)c->ctx;
    if (a >= 0xC000){
        s->ram[a & 0x1FFF] = v;
        if (a == 0xFFFD) s->page[0] = (uint8_t)(v % s->num_banks);   /* slot 0 bank */
        else if (a == 0xFFFE) s->page[1] = (uint8_t)(v % s->num_banks);
        else if (a == 0xFFFF) s->page[2] = (uint8_t)(v % s->num_banks);
        /* $FFFC = RAM-mapping control — not modelled */
    }
    /* writes to the ROM area are ignored (mapper regs are the $FFFC-$FFFF above) */
}

/* ---- I/O ports ---- */
static uint8_t sms_in(cpu_z80_t *c, uint16_t port){
    sms_t *s = (sms_t *)c->ctx;
    uint8_t p = (uint8_t)(port & 0xFF);
    if (p == 0xBF || p == 0xBD){                            /* VDP status */
        s->vdp_status_reads++;
        uint8_t st = s->vdp_status;
        s->vdp_status &= (uint8_t)~0x80u;                   /* reading clears frame flag */
        s->vdp_latch = 0;
        return st;
    }
    if (p == 0xBE || p == 0xBC){ s->vdp_latch = 0; return 0; }   /* VDP data */
    if (p == 0x7E) return s->vcounter;                      /* V counter (advances per frame) */
    if (p == 0x7F) return 0;                                /* H counter */
    return 0xFF;                                            /* controllers: no buttons */
}
static void sms_out(cpu_z80_t *c, uint16_t port, uint8_t v){
    sms_t *s = (sms_t *)c->ctx;
    uint8_t p = (uint8_t)(port & 0xFF);
    if (p == 0xBE || p == 0xBC){ s->vdp_data_writes++; s->vdp_latch = 0; return; }  /* VDP data */
    if (p == 0xBF || p == 0xBD){                            /* VDP control (2-byte) */
        if (!s->vdp_latch){ s->vdp_first = v; s->vdp_latch = 1; }
        else {
            s->vdp_latch = 0;
            uint8_t code = (uint8_t)(v >> 6);
            if (code == 2){                                 /* register write */
                uint8_t reg = (uint8_t)(v & 0x0F);
                s->vdp_reg[reg] = s->vdp_first;
                s->vdp_reg_writes++;
            } else {
                s->vdp_addr = (uint16_t)(((v << 8) | s->vdp_first) & 0x3FFF);
                s->vdp_code = code;
            }
        }
        return;
    }
    /* $7F PSG, $3E/$3F memory/IO control, $DC/$DD controllers — accepted, no-op */
}

int sms_load(sms_t *s, const uint8_t *img, uint32_t len){
    for (unsigned i = 0; i < sizeof *s; i++) ((uint8_t*)s)[i] = 0;
    if (len == 0) return 0;
    /* Some dumps carry a 512-byte header — strip it if present (odd 512 remainder). */
    uint32_t off = (len % 0x4000u == 512u) ? 512u : 0u;
    uint32_t n = len - off;
    if (n > SMS_ROM_MAX) n = SMS_ROM_MAX;
    for (uint32_t i = 0; i < n; i++) s->rom[i] = img[off + i];
    s->rom_size = n < 0x4000u ? 0x4000u : n;                /* at least one bank */
    s->num_banks = s->rom_size / 0x4000u; if (s->num_banks == 0) s->num_banks = 1;
    s->page[0] = 0; s->page[1] = (uint8_t)(1 % s->num_banks); s->page[2] = (uint8_t)(2 % s->num_banks);
    s->cpu.read = sms_rd; s->cpu.write = sms_wr;
    s->cpu.in = sms_in;   s->cpu.out = sms_out;  s->cpu.ctx = s;
    return 1;
}

void sms_run(sms_t *s, uint32_t budget, uint32_t frame_period){
    cpu_z80_reset(&s->cpu);                                 /* Z80 boots at $0000 */
    if (frame_period == 0) frame_period = 3000;
    uint32_t i = 0;
    for (; i < budget && !s->cpu.jammed; i++){
        /* V counter advances 0..~261 across each frame so vcounter-wait loops
         * (how SMS games time vblank) make progress. */
        s->vcounter = (uint8_t)(((i % frame_period) * 262u / frame_period) & 0xFF);
        if (cpu_z80_step(&s->cpu) == 0){
            if (s->cpu.halted){                             /* HALT waits for the IRQ */
                s->vdp_status |= 0x80u;
                if (s->vdp_reg[1] & 0x20u){ cpu_z80_int(&s->cpu); s->frame_ints++; }
                continue;
            }
            break;
        }
        if ((i % frame_period) == (frame_period - 1)){       /* frame boundary */
            s->vdp_status |= 0x80u;                          /* frame interrupt pending */
            if (s->vdp_reg[1] & 0x20u){ cpu_z80_int(&s->cpu); s->frame_ints++; }
        }
    }
    s->insn = i;
}

int sms_is_running(const sms_t *s){
    if (s->insn == 0) return 0;
    uint32_t ill_permille = (uint32_t)((uint64_t)s->cpu.illegal * 1000u / s->insn);
    /* Two independent liveness signals, each conclusive on its own:
     *  - runs a frame-interrupt-driven main loop (>=2 frame IRQs taken), or
     *  - drives the VDP heavily (register writes / status polls / VRAM upload)
     *    while executing mostly-legal code. The illegal gate is looser here
     *    than NES because a live SMS game that wanders a little after init is
     *    still unmistakably running once it's taking frame IRQs. */
    if (s->frame_ints >= 2 && ill_permille < 400u) return 1;
    if (ill_permille >= 250u) return 0;                     /* wandering through data */
    if (s->vdp_reg_writes >= 2) return 1;                   /* programmed VDP registers */
    if (s->vdp_status_reads >= 16) return 1;                /* polled VDP status a lot  */
    if (s->vdp_data_writes >= 64) return 1;                 /* uploaded VRAM            */
    return 0;
}

/* Self-check: a minimal SMS program — set stack, program VDP reg 0/1 (enable
 * frame IRQ), EI, then HALT-loop waiting for the interrupt. Proves the machine
 * runs a real SMS init pattern ON TARGET without an external ROM. */
int sms_selfcheck(void){
    static const uint8_t code[] = {
        0x31,0x00,0xDF,       /* LD SP,$DF00              */
        /* VDP reg 0 = $04 */
        0x3E,0x04, 0xD3,0xBF, 0x3E,0x80, 0xD3,0xBF,   /* OUT($BF),$04 ; OUT($BF),$80|0 */
        /* VDP reg 1 = $20 (frame IRQ enable, bit5) */
        0x3E,0x20, 0xD3,0xBF, 0x3E,0x81, 0xD3,0xBF,   /* OUT($BF),$20 ; OUT($BF),$80|1 */
        0xFB,                 /* EI                        */
        0x76,                 /* HALT (wait for frame IRQ) */
        0x18,0xFD             /* JR -3 (back to HALT)      */
    };
    static sms_t s;
    static uint8_t img[0x4000];
    for (unsigned i = 0; i < sizeof img; i++) img[i] = 0;
    for (unsigned i = 0; i < sizeof code; i++) img[i] = code[i];
    if (!sms_load(&s, img, sizeof img)) return 0;
    sms_run(&s, 20000u, 3000u);
    return sms_is_running(&s) ? 1 : 0;
}

/* ---- DECLARATION -----------------------------------------------------------

 * REQUIRES(z80_cpu_ready) is the whole of this module's boundary, measured:
 * sms.o's `nm -u` is exactly {cpu_z80_reset, cpu_z80_step, cpu_z80_int}. A
 * console core is its own glue plus somebody else's CPU.
 *
 * The bring-up is sms_selfcheck(), which already existed in this file -- one
 * of only two self-checks anywhere in the newly wired set. Calling the
 * module's own check is strictly better than inventing a new one beside it,
 * and it is what roots the Z80 core: sms_selfcheck runs the CPU.
 *
 * MIND THE POLARITY. sms_selfcheck() returns 1 FOR SUCCESS (the machine is
 * running) and 0 for failure. modbind_selfcheck() -- same name shape, same
 * file extension, opposite convention -- returns a COUNT OF PROBLEMS, where 0
 * is the good answer. A zxv_bringup_fn is the second kind: 0 means up. Writing
 * `(sms_selfcheck() == 0) ? 0 : -1` here inverted it, and the first boot with
 * these declarations reported `failed=1` on the one module whose check was
 * actually passing. It cost nothing to find only because the failure was
 * printed BY NAME; as a bare count it would have meant bisecting 87 modules.
 * Do not infer a return convention from a function's name.
 */
#include "zxv_decl.h"
static int zxvd_sms_bringup(void) {
    return sms_selfcheck() ? 0 : -1;   /* 1 = running = up */
}

ZXV_DECLARE(sms,
    ZXV_PROVIDES(sms_console_ready),
    ZXV_REQUIRES(z80_cpu_ready),
    ZXV_BRINGUP(zxvd_sms_bringup));
