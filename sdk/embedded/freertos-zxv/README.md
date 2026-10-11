<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# freertos-zxv: ZXV in a FreeRTOS owner task

This kit runs ZXV inside one FreeRTOS task. That task owns every ZXV object:
the context, capabilities, the rmag posting budget, the phase clock and a
`zxe_ledger`. Other tasks reach it through a request queue. A periodic
**budget tick** refills the posting budget and advances the phase clock from
the RTOS tick.

The kit runs on the **FreeRTOS POSIX simulator** (Linux). It has not been
run on a microcontroller (see [../README.md](../README.md), "Not done").

**Advisory only.** Nothing here actuates. Read
[../README.md, "Safety"](../README.md#safety-advisory-only) before putting
ZXV next to anything that does.

## FreeRTOS is fetched, not vendored

`make fetch` clones **FreeRTOS-Kernel V11.2.0** into `build/FreeRTOS-Kernel`,
which is git-ignored, and checks that the tag resolves to commit
`0adc196d4bd52a2d91102b525b0aafc1e14a2386`. It needs network access once. To
use an existing checkout instead:

```sh
make FREERTOS_KERNEL_PATH=/path/to/FreeRTOS-Kernel test
```

## Build and run

```sh
cd sdk/embedded/freertos-zxv
make test          # fetch (once), build, run the three scenarios
make SANITIZE=1 test
make clean
```

Expected output:

```
burst of 8 postings: 5 accepted, 3 refused by the budget
phase tick 55440, budget remaining 4/1, ZXV heap live 58896 bytes (peak 58896)
freertos-zxv: 14 checks, 0 failures
freertos-zxv --no-entropy: 2 checks, 0 failures (ZXV stayed closed)
rogue task calls libzxv directly...
zxp fatal: libzxv called from a task other than the owner (ZXP_NO_TLS build)
--violate: stopped by the single-owner check (exit 70)
```

The three scenarios (`main_posix.c`):

| run | shows |
|---|---|
| default | A client task posts through the owner task (issuer → grid → meter), reads balances, and bursts 8 postings into a 5-per-period budget. 3 are refused (`ZXV_E_BUDGET`), and the next tick refills the budget. |
| `--no-entropy` | No entropy source is installed. `zxp_init()` is never called, the owner task answers every request with `ZXV_E_INTERNAL`, and nothing is posted (fail closed). |
| `--violate` | A rogue task calls libzxv directly. The port's single-owner check calls `zxp_fatal()` and the program stops with exit 70. |

The simulator's entropy is the host's `getrandom()`, which is **for the
simulator only**. On a board, replace `host_entropy` with your TRNG driver.

## The task API (`zxv_task.h`)

```c
zxp_set_entropy(board_trng_read, NULL);     /* integrator's responsibility */
zxp_init();                                 /* fail closed if the TRNG is unhealthy */

zxv_task_config cfg = { .tick_period_ms = 100, .postings_per_period = 5,
                        .phase_ticks_per_second = 1000, .currency = "EUR", .minor_units = 2 };
zxv_task_start(&cfg, tskIDLE_PRIORITY + 2, configMINIMAL_STACK_SIZE * 2);

zxv_req r = { .kind = ZXV_REQ_POST, .from = ZXV_ACCT_ISSUER_ID, .to = ZXV_ACCT_GRID_ID,
              .amount_minor = 1250, .reference = "meter-42/0001" };
if (zxv_task_call(&r, pdMS_TO_TICKS(100)) == pdTRUE && r.status == ZXV_OK) { /* r.entry_id */ }
```

What a request does:

- **POST** consumes one unit of the rmag budget `postings`, then posts with
  `zxe_ledger_post`. If the posting itself is refused (for example
  insufficient funds or a duplicate reference), the unit is released.
- **BALANCE** returns an account's equity.
- **STATS** returns the remaining budget (an exact rational), the phase tick
  and the ZXV heap's live and peak bytes.

Call `zxv_task_call` from tasks only: never from an interrupt, and never from
the owner task itself.

## Porting to a board

The libraries build with `../lib.mk`:

```sh
make -C .. -f lib.mk CC=arm-none-eabi-gcc AR=arm-none-eabi-ar OUT=$PWD/build/fw \
     PORT=freertos NO_TLS=1 \
     TARGET_CFLAGS="-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -I<dir of your FreeRTOSConfig.h> \
                    -I<FreeRTOS-Kernel>/include -I<FreeRTOS-Kernel>/portable/GCC/ARM_CM4F"
```

Then link `libzxe.a libzxv_embedded.a libzxp.a` in that order.

Requirements in `FreeRTOSConfig.h`:

| setting | why |
|---|---|
| `configUSE_MUTEXES 1` | libzxv's mutexes become FreeRTOS mutexes |
| `configSUPPORT_STATIC_ALLOCATION 1` (recommended) | each mutex lives inside its libzxv object (`xSemaphoreCreateMutexStatic`); a `static_assert` checks it fits |
| `configSUPPORT_DYNAMIC_ALLOCATION 1` | libzxv's heap use goes to `pvPortMalloc` by default (or set your own with `zxp_set_allocator`) |
| `configTOTAL_HEAP_SIZE` | at least the ZXV heap you need (about 60 KB for ctx + rmag + phase on 32-bit; see ../README.md "RAM") plus your tasks |
| `configASSERT` defined | `zxp_fatal()` ends in `configASSERT(0)` and then loops. Use `zxp_set_fatal_handler()` to put the device in its safe state first. |

Notes:

- **Single owner.** FreeRTOS has no thread-local storage, so the build uses
  `ZXP_NO_TLS`. Only the owner task may call libzxv. That is why this kit is
  shaped as an owner task with a queue.
- **Before the scheduler starts,** locks are no-ops, so you may create ZXV
  objects in `main()`. The owner is then the task that first locks; or call
  `zxp_set_owner_current()` in the owner task.
- **Time:** the port extends the RTOS tick to 64 bits for
  `zxp_monotonic_us()`. Call it at least once per tick-counter wrap (about
  49 days at 1 kHz with a 32-bit tick); the budget tick does this.
- **Timing:** there are no real-time guarantees. Give the owner task a
  priority below anything with a deadline.
