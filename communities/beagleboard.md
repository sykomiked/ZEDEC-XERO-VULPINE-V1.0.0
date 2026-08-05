# ZXV → BeagleBoard / BeagleV

*A presentation for the people who do hard real-time on open silicon.*

## Who you are

BeagleBone and BeagleV. Your boards are famous for the **PRUs** —
programmable real-time units meant for exact, deterministic timing — because
your community does robotics, motor control, drones, instrumentation: work where
"eventually" is a failure and jitter is a bug you can measure on a scope.

## Why we are here

Most operating systems fight the real-time problem by bolting priorities and
patches onto a clock-tick scheduler that was never designed for determinism.
ZXV starts from the other end. Its runtime — **OSEQ, the Event-Sequence
Runtime** — does not advance on clock ticks at all. It advances on **discrete
event cycles**. A node fires when its inputs are ready; nodes in the same wave
are concurrent; the wave count *is* the critical path.

If you have spent time coaxing determinism out of a general-purpose kernel, an
OS whose ordering primitive is the event sequence itself — where the wall clock
is an *interoperability convenience, never a dependency* — is worth an
afternoon.

## The specific hook

We built something we think your community specifically will want to abuse: the
**Reality Engine**. A ZXV "sigil" is a real dataflow circuit — nodes on a graph
whose arithmetic (`gcd(N,k)`) literally sets how many independent parallel
*lanes* the computation has. Bind that circuit's inputs to **live sensed
variables** and it evaluates once per event tick, firing reactions when an
output crosses a threshold. It is a clockless, shape-defined, live
signal-processing graph — and it refuses to fabricate a sensor reading when no
backend is bound. Point it at a real ADC and the same graph runs on real signal.

For a PRU crowd, that is a new way to describe a deterministic pipeline.

## The honest ask

ZXV boots on ARM64 today; BeagleV is RISC-V, which currently **compiles but does
not yet boot** (the arch layer is unfinished — we say so). If a deterministic,
event-sequenced OS on RISC-V is interesting enough that you'd want to help push
the boot up, that is the most valuable contribution we could get from your
community. Or just run the Reality Engine's tests and tell us the model is
wrong.

> The clock is optional. The event order is not.
