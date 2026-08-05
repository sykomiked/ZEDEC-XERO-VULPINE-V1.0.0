# External-Clock Bridge — Event-Space Architecture Rule

**Status:** Enforced convention, not aspirational. Every claim below is
backed by code that builds, boots, and has been observed running in
this repo (see "Verification" at the bottom).

## The rule

This kernel's execution model is event-space (`ordinal_t`/`cycle_pulse_t`/
`phase_tick_t`, see `kernel/include/m5_types.h`), not wall-clock. But
real hardware — the ARM generic timer, GPS/GNSS satellite timing, a
cellular modem's frame clock, a DLP projector's mirror-actuator
resonant frequency, UART baud clocks — is fundamentally, physically
clocked. Both are true at once. The rule that reconciles them:

> **Raw clock signals may only be touched inside an ISR or a
> peripheral's own register-level driver code. Nothing above that
> boundary — schedulers, economic subsystems, application logic,
> other device models — may read a wall clock or poll a hardware
> timer directly. It only ever sees `cycle_pulse_t`/`phase_tick_t`
> events, or a device's own `_tick()` call driven by that event
> stream.**

This is not a style preference — the ARM64 boot chain in this repo
was, until this session, silently broken specifically because that
boundary didn't exist end-to-end (see below), and the fix is the
concrete reference implementation for every future driver.

## Reference implementation: the generic timer

```
[Physical timer hardware]              <- real clock, ARMv8-architected
        |  CNTP_CTL_EL0 fires (ENABLE=1, IMASK=0)
        v
[GICv3: redistributor SGI_base + distributor GICD_CTLR(ARE|G1|G0)]
        |  routes PPI 30 as a Group-1 IRQ
        v
[boot.s: irq_handler_curr_elx -> irq_handler_c()]   <- ISR boundary
        |
[gicv3.c: gic_handle_irq()]             <- IAR read, dispatch, EOI
        |  calls the registered handler for IRQ 30:
        v
[arm64_timer.c: arm64_timer_handler()]  <- reloads CNTP_TVAL, timer_ticks++
        |
        v  (WFI in the event loop now legitimately wakes)
[kernel_main_arm64.c: phase_coordinator_tick(&tick)]  <- EVENT-SPACE BOUNDARY
        |
        v
[cycle, tick.omega, ... everything else in the kernel]
```

Everything below `phase_coordinator_tick()` — the DLP projector's
`dlp_projector_tick()`, the Vino ledger's `vino_propose_block()`
cadence, the scheduler (once event-driven, see TODO #4) — reads only
`cycle`/`tick`, never `CNTPCT_EL0` or any MMIO timer register directly.

### What was actually broken (fixed this session)

`kernel/compat/compat_layer.c` declares `irq_handler_c()` and
`exception_handler()` as **weak no-op stubs**, explicitly meant to be
overridden per architecture. The ARM64 port never provided that
override, so the ISR boundary above didn't exist: every IRQ vector
landed on a function that did nothing — no `gic_handle_irq()`, no
timer reload, no EOI. `WFI` never durably woke, and the entire
event-cycle loop hung at `cycle=0` forever, even though every
boot-time console message printed correctly first.

Fixed in `kernel/arch/arm64/arm64_exceptions.c` (new file, provides
the strong override) plus two real GICv3 configuration bugs in
`kernel/arch/arm64/gicv3.c`:
- PPIs/SGIs (IRQ 0-31, including the timer) live in the redistributor's
  *SGI_base* frame (`RD_base + 0x10000`), not the distributor — the
  driver only ever configured SPIs there.
- `GICD_CTLR` was written as `1` (EnableGrp0 only); `EnableGrp1` and
  `ARE` were never set, so no Group-1 interrupt — which is everything
  this driver configures — could leave the distributor.

## Reference implementation: the DLP projector

`kernel/src/hardware/dlp_projector.c` is the second, deliberately
parallel example. A DLP wobulation actuator is *also* a piece of
externally-clocked hardware (a mechanical MEMS resonator with its own
required frequency — see `dlp_projector_verify_actuator_sync()`, a
real physics feasibility check: `actuator_freq_hz >= max_input_hz *
shift_x * shift_y`). It is bridged the same way as the timer:
`dlp_projector_tick(&projector)` is called once per event cycle from
`kernel_main_arm64.c`'s main loop, immediately after
`phase_coordinator_tick()` — never from an independent poll loop.

## Applying this to what's not built yet

Every remaining externally-clocked peripheral in the Tank 5 / Tank Pad
target line follows the identical pattern when implemented:

| Peripheral | Real external clock | Bridge point |
|---|---|---|
| GPS/GNSS | Satellite atomic-clock-derived PPS/time signal | ISR on the PPS GPIO/IRQ line -> one `gnss_tick()` per event cycle, exposing only a resolved fix, never raw satellite time, above the ISR |
| Cellular modem | Network frame clock (LTE/5G frame timing) | Modem IRQ -> `modem_tick()` |
| Wi-Fi/BT | Radio MAC clock | Radio IRQ -> `radio_tick()` |
| UART (MediaTek boards) | `uart_clock_hz` per `board_profile.h` | Already ISR-driven via the same GICv3 path once wired for the real board |

`kernel/arch/arm64/board_profile.h` (added this session) is the data
side of this for multi-board support: physical addresses/clocks are
data attached to a board profile, not forked copies of the boot/driver
code, in the same "hardware is code" spirit as
`kernel/src/hardware/rtl_device.h`.

## Verification

- `Makefile.arm64` build: clean (`make -f Makefile.arm64 clean && make
  -f Makefile.arm64 LIBGCC=...`).
- Boot: `qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m
  256M -kernel kernel_arm64.elf -nographic -monitor none -serial
  stdio` — full boot sequence, then a live `tick: omega=N cycle=N`
  stream at ~100Hz sustained through 2000+ cycles, confirmed in this
  session.
- Host tests: `make test` in `kernel/` — all pass except the
  pre-existing, unrelated `test_rmag` failure.
