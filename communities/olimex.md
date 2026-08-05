# ZXV → Olimex

*A presentation for Europe's oldest open-hardware house, and the people who put
their boards in factories.*

## Who you are

Since the 1990s you have built open-source SBCs and microcontrollers that go
into *industrial* environments — machines that must not fall over, in places
where a reboot is expensive and a field visit is worse. OLinuXino, the A64 and
RK3328 boards, the AVR/ARM dev tools. You certify for temperature ranges most
"maker" boards never see.

## Why we are here

Industrial means *fault isolation*. A driver that panics should not take the
line down. ZXV's **cellular multikernel** is built for exactly this: the system
runs as a fabric of isolated cells, each with its own contract, and a cell that
faults is fail-stopped and its routes revoked **while the fabric keeps running**
— then re-admitted with a new incarnation. We demonstrate this at every boot: a
cell is deliberately failed, contained, and recovered, live, in the log.

For a board that controls something that matters, an OS whose failure mode is
"isolate and continue" rather than "kernel panic" is worth a serious look.

## The specific hook

Two things aimed straight at industrial edge:

- **Deterministic sequencing.** ZXV advances on discrete *event-phase ticks*,
  not a wall clock. Timing is a property of the event order, which is
  reproducible. For control loops and audit, "what happened, in what order" is
  recoverable by construction.
- **Legacy interoperability without legacy weight.** ZXV speaks standard TCP/IP
  (verified against a real internet host) and is designed to bridge to older
  fieldbus and representation formats through adapter layers, without dragging a
  general-purpose Linux userland onto a constrained board.

## The honest ask

ZXV boots on ARM64 today — the same class of SoC as your A64/A20 boards. It does
**not** yet have drivers for your specific peripherals; the driver model is a
thin ops-boundary over portable, tested logic (the pattern our virtio driver
already uses), so writing one for an Olimex board is a bounded job, not a
rewrite. If fault-isolated edge computing on open silicon is a thing your
customers ask for, let's put ZXV on an OLinuXino and try to make a cell die
without taking the board with it.

> An OS for boards that are not allowed to crash.
