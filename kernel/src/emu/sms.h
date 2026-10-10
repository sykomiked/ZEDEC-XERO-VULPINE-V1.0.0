/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sms.h — a Sega Master System / Game Gear machine around the Z80 core.
 *
 * The second faithful per-console machine (after NES), and the first on the Z80
 * side — extending real "is the game running" verification to the Z80 slice
 * (Master System, Game Gear, and the same shape as SG-1000/ColecoVision). Gives
 * the Z80 its true environment: the Sega ROM mapper (three 16KB slots paged via
 * $FFFD-$FFFF, first 1KB of bank 0 fixed), 8KB work RAM ($C000, mirrored), the
 * VDP register/data ports ($BE/$BF), the PSG/VCounter ports, controller ports,
 * and the per-frame VDP interrupt. Real game code then follows its real path:
 * programs the VDP registers, uploads VRAM, enables the frame interrupt, and
 * runs its main loop on that interrupt. The RUNNING verdict is BEHAVIOURAL
 * (VDP configured + status polled / frame-IRQ driven, at a low illegal rate),
 * which random data cannot fake — same discipline as the NES machine.
 *
 * Scope: the CPU-visible machine (mapper + ports + frame IRQ) — enough to run
 * init + main loop and OBSERVE liveness. VDP rendering, PSG audio, and Game Gear
 * specifics are not modelled. */
#ifndef ZXV_SMS_H
#define ZXV_SMS_H

#include <stdint.h>
#include "cpu_z80.h"

#define SMS_ROM_MAX (512u * 1024u)

typedef struct sms {
    cpu_z80_t cpu;
    uint8_t   ram[8192];               /* $C000-$DFFF, mirrored to $FFFF */
    uint8_t   rom[SMS_ROM_MAX];
    uint32_t  rom_size;
    uint32_t  num_banks;               /* rom_size / 16KB (>=1)          */
    uint8_t   page[3];                 /* slot 0/1/2 bank selects        */
    /* VDP */
    uint8_t   vdp_reg[16];
    uint16_t  vdp_addr;
    uint8_t   vdp_code, vdp_latch, vdp_first, vdp_status, vcounter;
    /* behavioural observation — the RUNNING signal */
    uint32_t  vdp_reg_writes;
    uint32_t  vdp_status_reads;
    uint32_t  vdp_data_writes;
    uint32_t  frame_ints;
    uint32_t  insn;
} sms_t;

/* Load a raw SMS/GG ROM image. Returns 1 on success. */
int  sms_load(sms_t *s, const uint8_t *img, uint32_t len);

/* Reset and run up to `budget` instructions, raising a VDP frame interrupt
 * every `frame_period` instructions (when the game enabled it). */
void sms_run(sms_t *s, uint32_t budget, uint32_t frame_period);

/* Behavioural verdict: did the game actually come alive on the machine? */
int  sms_is_running(const sms_t *s);

/* On-target self-check with a built-in minimal SMS program. Returns 1 on pass. */
int  sms_selfcheck(void);

#endif /* ZXV_SMS_H */
