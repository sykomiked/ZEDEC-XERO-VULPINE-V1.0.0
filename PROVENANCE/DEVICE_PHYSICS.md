# Device physics — impedance matching as the performance model

_Owner directive: code each subsystem from how the hardware physically works._

---

## 0. It is not overclocking, and the real name is better

Matching code to hardware behaviour does not raise a clock. It removes an
**impedance mismatch** — and by the maximum power transfer theorem, power
delivered is greatest when source and load impedance match. Code whose access
pattern is out of phase with the device's native pattern draws current and does
no work.

That is the same argument as `mb_real_power`, one layer down. A memory-bound
loop with the wrong stride is not slow because the CPU is slow; it is slow
because it is **reactive** — busy, drawing power, delivering nothing. The gains
are real and often large, and they come from removing waste rather than from
exceeding a rating.

## 1. LCD — a shutter, not a lamp

An LCD does not emit. A backlight is on continuously and each subpixel is a
**twisted-nematic shutter** modulating it. What follows is not stylistic:

| physical fact | code consequence |
|---|---|
| backlight is always on | **black costs the same power as white.** A dark theme saves nothing on LCD — it does on OLED, which is emissive and switches per pixel. Two different display types need two different power policies, and treating them alike wastes one of them. |
| the crystal only twists where the value **changes** | **damage tracking is not an optimisation, it is the physical model.** Redrawing an unchanged pixel commands a shutter to hold its current position — a null instruction with full cost. |
| panel refresh is fixed | rendering faster than refresh is discarded work. Frame pacing should be phase-locked to the panel, not free-running. |
| response time is mechanical (crystal rotation) | there is a floor on transition speed that no amount of compute removes. Below it, effort is wasted. |
| addressing is row/column scanned | updates aligned to scan order cost less than scattered ones — the same reason sequential memory access beats random. |

**The tree already has the defect this predicts.** `zxv_shell.h:35-36` sizes
`FIELD_TX 80 / FIELD_TY 45` for 1280x720 and indexes to 7279 against a
3600-entry array — 43.3% of the first screen corrupt, reproduced at 624 of 1440
rows. That is a geometry model disagreeing with the panel's actual geometry.
Damage tracking would not merely speed this up; it would have made the
disagreement visible immediately, because a damage rect cannot exceed the panel.

## 2. Flash storage — erase blocks, not bytes

NAND cannot overwrite. A write means erase-then-program, and the **erase block is
far larger than the program page**. Rewriting one byte costs a whole block.

| physical fact | code consequence |
|---|---|
| erase granularity >> write granularity | **copy-on-write and log-structured writes match the physics exactly.** `zxvfs` already does CoW — that was chosen for crash atomicity and it is also the correct flash discipline. Two reasons, one design. |
| blocks wear out | write amplification is a lifetime cost, not just a speed cost. Journalling metadata only (which `zxvfs` does) is the right call twice over. |
| reads are cheap, erases are expensive and slow | read-heavy structures should be denormalised; write-heavy ones appended. |
| the FTL relocates behind your back | "sequential on disk" is a fiction above the FTL. Do not optimise for a layout you do not control — optimise for *fewer, larger, aligned* writes. |

## 3. Processor — the cache is the machine

The core is rarely the bottleneck; the memory hierarchy is.

| physical fact | code consequence |
|---|---|
| memory moves in **cache lines** (64 B typical), never single bytes | a struct that straddles lines costs two fetches. Layout is throughput. |
| prefetchers detect **linear** strides | an array walked in order is many times faster than the same data chased through pointers. Data-oriented beats object-oriented for bulk work. |
| a mispredicted branch discards the pipeline | branchless arithmetic can beat a "cheaper" conditional. This is why the exact-integer predicates in `zphi`/`rat` are fast *and* correct — no epsilon, no branch on float. |
| two cores writing one line = **false sharing** | in a multikernel this is decisive: replicated per-core state is not merely a design preference, it is how you avoid silently serialising on a shared line. |

That last row is the strongest vindication of the multikernel choice — replicated
state was argued from architecture, and the cache-coherence physics demands the
same thing independently.

## 4. The general rule, and the honest limit

**Model the device, then write the code the model implies.** Every row above is a
measurable consequence, not an aesthetic.

The limit worth stating: this is only free when the model is *correct*. Coding to
an imagined physics is worse than coding to none, because it produces confident
structure aimed at the wrong constraint — exactly the failure the display
geometry above already demonstrates. So each device model here should be
**validated against the real device** before code is optimised for it, in the same
way every other claim in this project now has to be.

## 5. Order of work

1. **Damage tracking in the shell** — physically motivated, fixes a live defect,
   and is already the prerequisite for remote display in SYSTEM_MAP.md §5.
2. **Phase-lock frame pacing to panel refresh** instead of free-running.
3. **Per-panel-type power policy** — emissive vs transmissive are not the same
   device and should not share one path.
4. **Cache-line audit** of the hot structures, starting with anything a
   multikernel replicates per core.
