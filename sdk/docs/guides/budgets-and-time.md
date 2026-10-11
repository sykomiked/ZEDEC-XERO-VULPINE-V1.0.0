<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Budgets and time

**Who this is for:** developers who need to cap how much of something code
may use (GPU seconds, API calls, plugin instructions), or schedule work on a
deterministic clock.
**What you will be able to do:** create a budget, spend from it, wait for it
to refill without busy-looping, and use the phase clock in tests and in real
time.

## Budgets

A **budget** has an exact limit (a [`Rational`](exact-numbers.md), so
`2.5` really means 2.5) and refuses any spend that would go over it. Budgets
live in a budget set (`rmag` in C) and need the `BUDGET` capability.

```python
import asyncio
import zxv
from zxv import Cap, Rational

with zxv.Context() as ctx:
    root = ctx.root_capability()
    with zxv.Budgets(root.attenuate(Cap.BUDGET)) as budgets:
        gpu = budgets.create("gpu-seconds", zxv.Resource.COMPUTE_CYCLES, "2.5")
        gpu.consume(Rational.of(5, 2))      # uses all of it
        gpu.remaining                       # 0
        gpu.try_consume(1)                  # False: no exception, just "no"
        gpu.consume(1)                      # raises BudgetExhaustedError (ZXV_E_BUDGET)
        gpu.consume(-1)                     # raises ZxvUsageError (ZXV_E_RMAG_INVALID)
```

`ZXV_E_BUDGET` is a refusal: wait, or give up. `ZXV_E_RMAG_INVALID` is a bug
in the caller (a negative amount): do not retry it.

### Waiting for a refill

`consume_async` waits until the budget has room, then spends. Room appears
when someone calls `release` or raises the limit. Both bump the budget's
**epoch** (a counter of refills), which wakes the waiters. There is no
polling.

```python
async def main():
    waiter = asyncio.create_task(gpu.consume_async(1, timeout=5))
    await asyncio.sleep(0.05)
    gpu.release(1)            # frees 1, wakes the waiter
    await waiter              # the consume succeeded
asyncio.run(main())

gpu.status                    # BudgetStatus(remaining=Rational(0, 1), used=Rational(5, 2), epoch=1)
```

You can also hear every denial: `budgets.on_exhausted(lambda b: ...)`.

The same in TypeScript (an amount may be a whole number, a `bigint`, text
or a `Rational`; a fractional `number` is refused):

```ts
using budgets = Budgets.open(root.attenuate(Cap.BUDGET));
const gpu = budgets.create('gpu-seconds', '2.5');
gpu.consume(Rational.of(5n, 2n));
const waiting = gpu.consumeAsync(1, { timeoutMs: 5_000 });
gpu.release(1);
await waiting;
```

In C, register a listener. It is called with `ZXV_RMAG_EV_DENIED`,
`ZXV_RMAG_EV_INVALID` or `ZXV_RMAG_EV_REPLENISHED`:

```c
static void on_budget(void *user, uint32_t budget, int32_t event) {
    (void) user;
    printf("budget %u: %s\n", budget, event == ZXV_RMAG_EV_DENIED ? "denied"
                                    : event == ZXV_RMAG_EV_REPLENISHED ? "replenished" : "invalid");
}

zxv_rmag_create_cap(bcap, &rm);
zxv_rmag_listen(rm, on_budget, NULL);
zxv_rmag_budget(bcap, rm, "gpu-seconds", ZXV_RES_COMPUTE_CYCLES, 5, 2, 1, &b);  /* limit 5/2, enforced */
zxv_rmag_consume(bcap, rm, b, 5, 2);
zxv_rmag_consume(bcap, rm, b, 1, 1);     /* ZXV_E_BUDGET; prints "budget 0: denied" */
zxv_rmag_release(bcap, rm, b, 1, 1);     /* prints "budget 0: replenished" */
```

The same budgets pay for plugin instructions: see
[Writing a plugin](writing-a-plugin.md).

## The phase clock

ZXV keeps its own time as a whole number of **ticks**. It does not read the
wall clock unless you ask it to. One **fundamental** cycle is 27,720 ticks
(`PHASE_CYCLE`). That number is the smallest one that 1, 2, 3, … 12 all
divide, so any cycle of 1 to 12 steps fits into it exactly.

Needs the `PHASE` capability.

### Deferring work

`defer(tick)` parks an event until the clock reaches that tick. The event
starts `PENDING` and becomes `CLEARED` when the clock gets there.

```python
with zxv.PhaseClock(root.attenuate(Cap.PHASE)) as clock:
    ev = clock.defer(clock.now + zxv.PHASE_CYCLE)
    clock.advance(zxv.PHASE_CYCLE - 1)
    ev.state.name                          # 'PENDING'
    clock.advance(1)
    ev.state.name, clock.now               # ('CLEARED', 27720)
```

### Two ways to drive it

- **By hand**, with `advance(n)`. Use this in tests and simulations: the same
  inputs always give the same results.
- **In real time**, with `start_realtime(ticks_per_second)`. A background
  task advances the clock as wall time passes and wakes waiters:

```python
async def main():
    ev = clock.defer(clock.now + 100)
    driver = clock.start_realtime(1000)    # 1000 ticks per second
    await ev.wait_cleared()
    await driver.stop()
asyncio.run(main())
```

TypeScript: `PhaseClock.open(cap)`, `clock.now()` (a `bigint`),
`clock.defer(...)`, `await ev.awaitCleared()`. C: `zxv_phase_create_cap`,
`zxv_phase_defer`, `zxv_phase_advance`, `zxv_phase_event`.

### Admitting steps

`clock.admit(phase_id, step_id)` asks the scheduler whether a step may run
now. It answers `ADMIT` (with a non-zero token), `VETO`, `DEFER` or `RETRY`.
`phase_id` must be 1 to 13, otherwise you get `ZXV_E_ARG`.

## Next

- [Writing a plugin](writing-a-plugin.md): budgets as plugin fuel.
- [SDK_DESIGN §8](../SDK_DESIGN.md#8-budgets-and-async) and
  [§9](../SDK_DESIGN.md#9-phase-ticks).
