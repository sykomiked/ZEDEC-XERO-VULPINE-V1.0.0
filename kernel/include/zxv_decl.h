/* zxv_decl.h — a module declares its OWN readiness contract, in its OWN
 * translation unit. The declaration is a property of the module; nothing
 * central lists the modules.
 *
 * WHY THIS SHAPE AND NOT A TABLE
 * ------------------------------
 * `composed_bringup()` in the arch main is external glue: it wires the modules
 * somebody remembered and is silent about the ones nobody did. A central table
 * of declarations has exactly the same defect one level up -- adding a module
 * and forgetting the table entry is still invisible. So the declaration lives
 * beside the code it describes, and the linker collects it:
 *
 *     ZXV_DECLARE(zxvfs,
 *         ZXV_PROVIDES(zxvfs_ready),
 *         ZXV_REQUIRES(blockdev_ready, mm_ready),
 *         ZXV_BRINGUP(zxvfs_bringup))
 *
 * No number. No wave. No position in a list. The dependency graph IS the
 * schedule (PROVENANCE/EVENT_SPACE_BRINGUP.md), and modbind_resolve() computes
 * readiness transitively, so these declarations are order-independent: any
 * order of registration yields the same fixpoint.
 *
 * WHY THE RECORDS ARE `const`
 * ---------------------------
 * modbind_register() takes the struct BY VALUE (`g_mods[g_n] = *m;`) and never
 * writes through the caller's pointer, so a declaration can be a compile-time
 * constant living in .rodata. That is what makes this possible at all in a
 * freestanding kernel: no runtime construction, no strcpy, no allocation, no
 * libc. A declaration costs nothing but the bytes.
 *
 * WHY THE SECTION HOLDS POINTERS, NOT RECORDS
 * -------------------------------------------
 * The walk advances by `p++` over a pointer array, so the stride is
 * sizeof(void *) by construction -- identical in every translation unit and on
 * every one of the five architectures, including the 32-bit ones. A section of
 * whole mb_module_t records would depend on every TU agreeing about struct
 * padding; a pointer array cannot go wrong that way.
 *
 * WHY THE SECTION IS NAMED `.rodata.zxv_decl` AND IS PLACED EXPLICITLY
 * -------------------------------------------------------------------
 * Two independent reasons, both measured:
 *   1. ORPHAN PLACEMENT HAS ALREADY COST THIS PROJECT A BOOT. kernel/arch/arm64/
 *      linker.ld:19-28 records .note.gnu.build-id landing at VMA 0x40080000 over
 *      the start of .text while attributed to the RW :data PHDR -- overlapping
 *      PT_LOAD segments, and QEMU refusing the kernel. So the section is placed
 *      by hand inside .rodata, ahead of the wildcards, with PROVIDE_HIDDEN
 *      markers -- never left for ld's orphan heuristics, and never relying on
 *      auto-generated __start_/__stop_ symbols (which ld only mints for section
 *      names that are valid C identifiers anyway).
 *   2. The `.rodata.` prefix means every linker script that does NOT yet carry
 *      the explicit placement still absorbs these records through its existing
 *      `*(.rodata.*)` wildcard. An arch that has not opted in gets inert bytes
 *      in .rodata, not a broken image.
 *
 * The marker symbols are WEAK for the same reason: an arch (or a host test)
 * that never placed the section links fine and simply walks nothing.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV composition slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_DECL_H
#define ZXV_DECL_H

#include "modbind.h"

/* GENERATED, one #define per declared module, by build_system/gen_phase_table.sh
 * into $(OBJDIR)/gen. It is not in the source tree and must never be committed:
 * a checked-in copy is a second source of truth for a number that is supposed to
 * be derived, and this tree's own DRC records that conventions here survive
 * HOURS. See ZXV_DECLARE at the bottom of this file for how it is used, and the
 * generator's header for why the number is the module's DRC layer. */
#include "zxv_phase_table.h"

/* A bring-up reports ONE OF THREE things, not two:
 *
 *   0                  UP (S+): the module is up. As it always was.
 *
 *   MB_BRINGUP_HELD    HELD (S0): the module's REQUIREMENTS were met but its
 *                      PRECONDITION or the HARDWARE it fronts is legitimately
 *                      ABSENT. This is NOT a fault -- it is the honest report of
 *                      an arch profile where, e.g., the MMU runs off. The module
 *                      leaves the "brought up" count and is named in S0 with its
 *                      reason, exactly like a requirements-held module.
 *
 *   any other non-zero FAILED: a genuine error or bug. Still raises [FAIL].
 *
 * WHY THE SENTINEL IS THIS VALUE. It must not collide with 0 (up) nor with an
 * errno-style negative a real failure returns (-1, -ENOSYS, ...). A large,
 * self-identifying positive magic does both: no bring-up in this tree returns a
 * bare positive, and 'HELD' is legible in a register dump. HELD is ONLY for
 * 'the hardware/precondition is legitimately absent' -- NEVER for 'something
 * went wrong'; a genuine bug must still return a plain non-zero and FAIL.
 *
 * It is NOT void: a bring-up that cannot report failure is indistinguishable
 * from one that was never called, and this project has been burned by that. */
#define MB_BRINGUP_HELD  ((int)0x48454C44)   /* 'HELD' -- see the three outcomes above */
typedef int (*zxv_bringup_fn)(void);

typedef struct {
    const mb_module_t *mod;      /* the constant declaration, in .rodata */
    zxv_bringup_fn     bringup;  /* may be NULL: declaring is not bringing up */
} zxv_decl_t;

/* ---- retention -----------------------------------------------------------
 * `used` stops -O2 discarding an unreferenced static before the linker ever
 * sees it. `retain` (SHF_GNU_RETAIN, GCC 11+) survives --gc-sections even where
 * the script has no KEEP(). Same two-tier guard as kernel/src/tolvovina/
 * tvl_rom.c:28-30, which is the only retention idiom already in this tree. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#  define ZXV_DECL_KEEP __attribute__((used, retain))
#else
#  define ZXV_DECL_KEEP __attribute__((used))
#endif
/* The section name is an ELF name. Mach-O requires "SEG,sect" and rejects this
 * outright, so a HOST test on macOS that happens to pull in a declaring TU fails
 * to compile — which is how test_mlkem_kat.c (it links keccak.c) first hit this.
 * On a host the declaration walk is meaningless anyway: nothing calls
 * zxv_decl_register_all(), and __zxv_decl_start/_end are weak and resolve to
 * nothing. So drop only the PLACEMENT off-target and keep `used`/`retain`, which
 * are what stop -O2 discarding the record. Every freestanding target this kernel
 * actually ships for is ELF and is unaffected — verify with:
 *   readelf -S kernel_arm64.elf | grep rodata.zxv_decl
 * If that section ever goes missing on a real target, this guard is too wide. */
#if defined(__APPLE__) || defined(__MACH__)
#  define ZXV_DECL_SECTION
#else
#  define ZXV_DECL_SECTION __attribute__((section(".rodata.zxv_decl")))
#endif

/* ---- capability spelling --------------------------------------------------
 * A capability is a bare token at the call site -- `mm_ready`, not "mm_ready" --
 * so a shell gate (build_system/verify_layers.sh) can grep the requires-graph
 * straight out of the .c files without parsing C.
 *
 * contract defaults to 1. Two modules PROVIDING one capability are alternative
 * provision and perfectly legal (modbind.h:146-153) -- but only at the same
 * contract, which modbind_verify_graph enforces as MB_ERR_CONTRACT. Use
 * ZXV_PROVIDES_AT when a provider deliberately moves to a new contract, so the
 * move is a build-visible act rather than a silent divergence.
 *
 * ---- AMPLITUDE: WHY THE DEFAULT IS FULL DRIVE AND NOT ZERO ----------------
 * The fields after `contract` are phase, amplitude and alternating, and this
 * macro used to expand all three to 0. Amplitude is a trit_t, and 0 is
 * TRIT_FALSE, which mb_real_power maps to a current of 0 permille -- so once the
 * resolver started asking the physics instead of comparing names, EVERY
 * capability in the system would have delivered exactly zero real power and all
 * 91 declared modules would have gone S0 at once. The default was a trap that
 * only sprang when the law was finally wired up.
 *
 * MB_AMP_FULL (== TRIT_TRUE, 1000 permille) is the right default because
 * DECLARING IS DRIVING. A module writing ZXV_PROVIDES(mm_ready) is asserting
 * that it supplies that capability; there is no weaker reading of the statement.
 * A module that means something less says so with ZXV_CAP_PHASED below, and
 * MB_AMP_NONE remains available for a capability that is declared and
 * deliberately not driven -- present in the graph, doing no work, which is a
 * thing this system can now express instead of a thing it did by accident.
 *
 * ---- PHASE: THE CAP RIDES ITS MODULE, IT DOES NOT CHOOSE -------------------
 * Phase used to expand to 0 here for every declaration in the tree, which made
 * dphase 0 on every edge, cos 1000 permille, and the physics an exact
 * reproduction of name-equality coupling. That was the right way to LAND the
 * law; it is not a resting place, because a power factor that is always 1000
 * measures nothing.
 *
 * The phase is now MB_PHASE_INHERIT: this capability rides whatever phase its
 * MODULE sits on, and modbind_register resolves the sentinel once. Two reasons,
 * one measured and one mechanical:
 *
 *   MEASURED. A phase derived from the CAPABILITY cancels. Provider and requirer
 *   would compute the same number from the same token, dphase would be 0 on
 *   every one of the graph's edges, and the change would be bit-identical to
 *   doing nothing. Phase has to be a property of the module or it is not a
 *   property at all.
 *
 *   MECHANICAL. ZXV_PROVIDES(...) and ZXV_REQUIRES(...) sit in ZXV_DECLARE's
 *   ARGUMENT LIST, and macro arguments are fully expanded before the body is
 *   substituted (ZXV_DECLARE applies neither # nor ## to them). The module name
 *   therefore CANNOT reach inside this macro -- verified against the real
 *   cross-toolchains, not assumed. ZXV_DECLARE stamps the module; the caps
 *   inherit. */
#define ZXV_CAP(c)          { #c, 1u, MB_PHASE_INHERIT, MB_AMP_FULL, 0u }
#define ZXV_CAP_AT(c, v)    { #c, (uint16_t)(v), MB_PHASE_INHERIT, MB_AMP_FULL, 0u }

/* The full electrical declaration, when a module means something other than
 * "in phase, fully driven": ride capability `c` at contract `v`, on l13 phase
 * `ph` (0..12), at drive level `amp` (one of the MB_AMP_* levels in modbind.h),
 * with `alt` non-zero if the two sides swap roles per phase.
 *
 * Written out rather than defaulted-by-omission on purpose: a declaration that
 * deviates from full drive in phase is making a claim about how it couples, and
 * that claim should be visible in one line at the declaration site rather than
 * inferred from which macro was used. */
#define ZXV_CAP_PHASED(c, v, ph, amp, alt) \
    { #c, (uint16_t)(v), (uint8_t)((ph) % MB_PHASES), (uint8_t)(amp), (uint8_t)(alt) }

#define ZXV_PROVIDES_PHASED(c, v, ph, amp, alt) \
    .provides = { ZXV_CAP_PHASED(c, v, ph, amp, alt) }, .n_provides = 1
#define ZXV_REQUIRES_PHASED(c, v, ph, amp, alt) \
    .requires = { ZXV_CAP_PHASED(c, v, ph, amp, alt) }, .n_requires = 1

#define ZXV_CAT_(a, b) a##b
#define ZXV_CAT(a, b)  ZXV_CAT_(a, b)

/* 1..MB_MAX_CAPS arguments, written out rather than recursed. A fifth argument
 * fails to compile, which is the correct outcome: modbind_register does NOT
 * bounds-check n_provides/n_requires against MB_MAX_CAPS, so an over-wide
 * declaration would otherwise be accepted and later read past the array. */
#define ZXV_CAPS1(a)             ZXV_CAP(a)
#define ZXV_CAPS2(a,b)           ZXV_CAP(a), ZXV_CAP(b)
#define ZXV_CAPS3(a,b,c)         ZXV_CAP(a), ZXV_CAP(b), ZXV_CAP(c)
#define ZXV_CAPS4(a,b,c,d)       ZXV_CAP(a), ZXV_CAP(b), ZXV_CAP(c), ZXV_CAP(d)
#define ZXV_NARG_(_1,_2,_3,_4,N,...) N
#define ZXV_NARG(...)   ZXV_NARG_(__VA_ARGS__, 4, 3, 2, 1, 0)
#define ZXV_CAPLIST(...) ZXV_CAT(ZXV_CAPS, ZXV_NARG(__VA_ARGS__))(__VA_ARGS__)

#define ZXV_PROVIDES(...)  .provides = { ZXV_CAPLIST(__VA_ARGS__) }, \
                           .n_provides = (uint8_t)ZXV_NARG(__VA_ARGS__)
#define ZXV_REQUIRES(...)  .requires = { ZXV_CAPLIST(__VA_ARGS__) }, \
                           .n_requires = (uint8_t)ZXV_NARG(__VA_ARGS__)

/* Explicit empty forms. A module that provides nothing or requires nothing must
 * SAY so -- an omitted clause would be indistinguishable from a forgotten one,
 * which is the whole defect this file exists to remove. */
#define ZXV_PROVIDES_NONE  .n_provides = 0
#define ZXV_REQUIRES_NONE  .n_requires = 0

/* Explicit contract override, one capability, for a deliberate version bump. */
#define ZXV_PROVIDES_AT(c, v)  .provides = { ZXV_CAP_AT(c, v) }, .n_provides = 1
#define ZXV_REQUIRES_AT(c, v)  .requires = { ZXV_CAP_AT(c, v) }, .n_requires = 1

#define ZXV_BRINGUP(fn)  (fn)
#define ZXV_NO_BRINGUP   ((zxv_bringup_fn)0)

/* ---- the declaration itself ----------------------------------------------
 * Three objects: the constant module record, the constant declaration that
 * pairs it with a bring-up, and the retained pointer the linker collects. The
 * bring-up function is reached only through a relocation FROM the retained
 * pointer, which is why a declared module needs no `-Wl,--undefined` GC root:
 * declaring it is what keeps it in the image. */
/* ---- WHERE THE PHASE COMES FROM ------------------------------------------
 * `.phase` is the ONLY number in a declaration that nobody types. It is pasted
 * from the generated table as ZXV_PHASE_OF_<mname>, and that table is computed
 * by build_system/gen_phase_table.sh from the module's DRC layer -- the same
 * layer verify_layers.sh already enforces statically. So the rigid build-time
 * gate and the fluid runtime law measure the SAME distance, in two registers.
 *
 * A COMPILE-TIME CONSTANT, WHICH IS NOT OPTIONAL: this initialiser lives in
 * .rodata (see "WHY THE RECORDS ARE const" at the top of this file), so the
 * phase must be an integer constant expression. A token-pasted #define is;
 * anything computed at runtime is not.
 *
 * A MODULE THE GENERATOR CANNOT SEE FAILS TO COMPILE, with
 * "'ZXV_PHASE_OF_<name>' undeclared". That is the intended and only acceptable
 * fallback. A default number would be an invented constant sitting silently
 * underneath the graph's most load-bearing edges -- and it would land exactly on
 * the modules the DRC never classified, which include both providers of
 * mm_ready and both of blockdev_ready. Loud beats silent. */
#define ZXV_DECLARE(mname, PROV, REQ, BRING)                                   \
    static const mb_module_t zxv_mod_##mname = {                               \
        .name = #mname, PROV, REQ,                                             \
        .phase = (uint8_t)(ZXV_CAT(ZXV_PHASE_OF_, mname) % MB_PHASES)          \
    };                                                                         \
    static const zxv_decl_t zxv_dcl_##mname = { &zxv_mod_##mname, BRING };     \
    static const zxv_decl_t *const zxv_dp_##mname                              \
        ZXV_DECL_SECTION ZXV_DECL_KEEP = &zxv_dcl_##mname

/* ---- the walk ------------------------------------------------------------
 * Order-independent by construction: the linker's collection order is an
 * accident of link line and is never load-bearing, because readiness comes from
 * the fixpoint, not from the walk. */

/* How many declarations the linker collected. 0 means the section was never
 * placed for this target -- report it, never treat it as "nothing to do". */
uint32_t zxv_decl_count(void);

/* Register every collected declaration. Returns how many were accepted, and
 * (when non-NULL) how many modbind_register REFUSED. A refusal is silent inside
 * modbind -- the table ceiling, a duplicate name and a malformed declaration all
 * return plain false -- so the count must be checked by the caller or the gate
 * would go on to verify a graph it cannot see. */
uint32_t zxv_decl_register_all(uint32_t *n_refused);

/* Run the bring-up of every declared module the fixpoint marked MB_READY.
 * Modules still MB_HELD are S0 -- reported, not an error, not silently skipped.
 * Returns how many bring-ups RAN AND SUCCEEDED; fills the failed/held counts.
 *
 * n_declared_only counts modules that are READY but declare NO bring-up
 * function. They ran nothing, so they are not in the return value. Counting
 * them as successes would make "every ready module brought itself up" true by
 * construction for each of them -- a claim that cannot fail is not a check.
 *
 * n_held counts modules NOT run: those the fixpoint left S0 (requirements not
 * satisfied), AND those whose bring-up RAN and returned MB_BRINGUP_HELD (their
 * requirements were met but the hardware/precondition is absent). A bring-up
 * HELD is NOT counted in the return value and NOT counted as failed. The held
 * modules that RAN can be recovered by name via zxv_decl_held_name below.
 * Pass NULL for any count you do not want. */
uint32_t zxv_decl_bringup_ready(uint32_t *n_failed, uint32_t *n_held,
                                uint32_t *n_declared_only);

/* How many failed bring-ups can be NAMED rather than merely counted. A count
 * on its own is not actionable -- finding the offending module would mean
 * bisecting by rebuild, which is exactly the silence this header exists to
 * remove (see gate_report_all() in the arch main, which exists for the same
 * reason one level up). Bounded and static: no allocation in a bring-up path. */
#define ZXV_DECL_MAX_FAILED 8u

/* Name of the i-th module whose bring-up reported failure during the most
 * recent zxv_decl_bringup_ready(), or NULL past the end. The name is the
 * declaration's own char[MB_NAME_LEN] and is NOT guaranteed NUL-terminated --
 * print it bounded. */
const char *zxv_decl_failed_name(uint32_t i);

/* Name of the i-th module whose bring-up RAN and returned MB_BRINGUP_HELD during
 * the most recent zxv_decl_bringup_ready(), or NULL past the end. These are S0 by
 * their own honest report (hardware/precondition absent), NOT by an unsatisfied
 * requirement -- the gate names them with that reason. Bounded and static, same
 * as the failed-name list. Print bounded: not guaranteed NUL-terminated. */
const char *zxv_decl_held_name(uint32_t i);

/* ---- THE BOOT GATE, ONCE, FOR EVERY ARCHITECTURE -------------------------
 * The sequence above (count -> register_all -> verify_graph -> report -> bring
 * up -> report) was written inline in kernel/arch/arm64/kernel_main_arm64.c and
 * therefore RAN ON ARM64 ALONE: the other four mains never called it, and their
 * boots printed no `declarations collected` line at all. The mechanism compiled
 * on five targets and executed on one.
 *
 * It lives in kernel/src/modbind/zxv_decl_gate.c now, and every arch main calls
 * this one function. Copying the block into four more mains would have produced
 * four copies of a check that exists precisely to catch drift -- and four copies
 * are four places to drift.
 *
 * ONE CALLBACK, NOT TWO. The five mains do not agree on a console: arm64 has
 * uart_puts AND uart_put_dec, arm32's main has no put_dec whatsoever (measured:
 * zero occurrences). So the gate takes the one primitive all five already have
 * and formats its own decimals -- with uint32_t arithmetic only, because a
 * 64-bit divide by a variable lowers to __udivdi3 and Makefile.riscv32 has no
 * libgcc to supply it. Same shape as tvl_bringup(tvl_puts_t) in
 * kernel/src/tolvovina/tvl_bringup.c, which bootfeat calls the same way. */
typedef void (*zxv_puts_t)(const char *);

/* Runs the WHOLE gate and prints every diagnostic through `puts`, each line
 * terminated with '\n'.
 *
 * Returns the TOTAL number of problems found -- 0 means the graph is sound AND
 * every bring-up that ran succeeded, so a caller can branch on the return value
 * without re-deriving anything. The total is a sum of four independent faults,
 * each also printed on its own line:
 *     +1        the linker collected ZERO declarations (section not placed) --
 *               a failure, never "nothing to do"
 *     +refused  declarations modbind would not accept (full/duplicate/malformed)
 *     +problems modbind_verify_graph's count (unprovided / cycle / contract)
 *     +failed   ready modules whose own bring-up returned non-zero
 * A NULL `puts` returns 1, not 0: a gate that could not report is a problem,
 * not a pass. */
uint32_t zxv_decl_boot_gate(zxv_puts_t puts);

#endif /* ZXV_DECL_H */
