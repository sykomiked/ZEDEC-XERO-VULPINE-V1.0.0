<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Power tracking

ZXV keeps an energy ledger for every app and every agent, on desktops,
laptops, phones and bare metal. It shows where the energy went, how the
battery is doing, and how hard the swarm should work. It also enforces
energy budgets that refuse work rather than price it.

The rule throughout is to report what was measured and to label everything
else. If a machine has no meter, the ledger says "none" and shows CPU time
instead. It never invents a wattage.

## Pieces

| Piece | Where | What it does |
|---|---|---|
| Ledger | `kernel/src/power/power_ledger.{c,h}` | Meters, interval totals, attribution per app and agent, battery, posture, budgets, JSON. Freestanding integer C, no allocation, no libc. |
| RAPL reader | `kernel/src/power/power_rapl.{c,h}` | x86 RAPL energy MSRs (Intel psys, package, DRAM, cores, uncore; AMD package) as ledger meters on bare metal. |
| Host reader | `kernel/arch/hosted/zxv_power_host.{c,h}` | Feeds the ledger on Linux, macOS and Windows. |
| `zxpower` | `kernel/arch/hosted/zxpower.c` | Command-line view: device power, battery, posture, top consumers. `--json` prints the ledger. |
| Temperature reader | `kernel/arch/hosted/zxv_thermal_host.{c,h}` | CPU, GPU, case/board and other temperatures from the host's own sensors, for zxvd's `thermal.read` (see "Temperatures" below). |
| Phone hook | `mobile/core/zxv_mobile_power.{c,h}` | Flat C API that the Kotlin and Swift shells call with what Android or iOS reports. Part of `libzxvcore.a`. |

## What each platform can measure

| Platform | Whole device | Per app | Battery |
|---|---|---|---|
| Linux laptop or desktop | RAPL psys, or package + DRAM (root only on kernels 5.10+); battery drain while unplugged | attributed by CPU time (`/proc`) | `/sys/class/power_supply`: charge, capacity, design, rate, cycles, temperature |
| macOS (Intel and Apple Silicon) | battery drain while unplugged; none on mains, so the total is then a marked lower bound | **metered** per process (`proc_pid_rusage` `ri_energy_nj`, macOS 12+) | IOKit AppleSmartBattery: voltage, amperage, capacities, cycles, temperature |
| Windows | battery drain while unplugged; none on mains | CPU time only (no public per-process energy API) | `CallNtPowerInformation(SystemBatteryState)`; design capacity not read yet |
| Android | battery drain from current x voltage | the ZXV app and its agents only | level, status, plug, current, voltage, cycles (Android 14+) |
| iOS | none (no current or energy API) | CPU time only | level and charging state only |
| Bare-metal x86 | RAPL psys, or package + DRAM | attributed by scheduler CPU time | not yet (needs an ACPI AML interpreter for `_BST`/`_BIX`, or an EC driver) |
| Bare-metal arm64 | none yet (SCMI sensors or board meters to come) | CPU time only | not yet |

On every platform, agents get their share of the energy of the app that runs
them (zxvd), split by the CPU and GPU time each agent used.

## How a figure is made

Each interval, `pw_settle` takes the device total from the meters:
- the largest **whole** meter (platform psys or battery drain) if any;
- otherwise the sum of the **part** meters (package, DRAM, GPU);
- **sub** meters (cores inside the package) are shown but never added.

Two whole meters that disagree by more than 10%, or parts that add up to more
than the whole, make a **conflict**. That is the lpres state BOTH. Both
readings are kept and the larger is used, so budgets err on the side of nature.

Energy that the platform metered per process goes to its app. The rest is
split by activity: the GPU pool by GPU time, and the rest by CPU time over the
interval's capacity (interval x CPUs). Idle and shared energy (screen, radios,
rounding) stays with the `system` consumer. The figures add up to the measured
total to the microjoule.

Each figure carries its source:

| Source | lpres state | Meaning |
|---|---|---|
| `metered` | TRUE | read off a meter |
| `attributed` | TRUE | a measured total, split by use |
| `conflict` | BOTH | meters disagree; both readings are kept |
| `fault` | FALSE | a reading was rejected (ran backwards, or past the meter's ceiling) |
| `none` | NEITHER | nothing measured |

## Battery and posture

The ledger derives health (full over design), time to empty and time to full
from what the platform reports. It also picks a posture, and the posture
scales the swarm's tokens per cycle by a Fibonacci ratio:

| Posture | When | Tokens per cycle |
|---|---|---|
| perform | on mains, or no battery | x 1 |
| balanced | on battery, above 20% | x 0.618 |
| conserve | 20% or below | x 0.382 |
| critical | 5% or below | x 0 (agent work waits) |

## Budgets

`pw_budget_set` gives an app, an agent or the whole device an energy budget
per rolling window. `pw_admit` answers:
- **OK**;
- **WAIT**: the window is spent, so the work waits for the next one;
- **HOLD**: a strict budget, and nothing measured to judge by.

An agent is bound by its own budget, its app's budget and the device budget.
No bid or fee buys past a budget: an ecological limit refuses, it does not
price.

## Trying it

```
cd kernel && make test-power            # ledger, RAPL, host fixture, phone hook
sh kernel/arch/hosted/test_zxpower.sh   # builds zxpower and reads this machine
```

The text view looks like this (illustrative numbers, not a measurement):

```
ZXV power  laptop / linux  8 CPUs  interval 1000 ms
device     14.212 W  metered
battery    71.4%  on battery  draining 14.090 W  3 h 2 min left  health 91.0%  312 cycles
posture    balanced (swarm tokens per cycle x 618/1000)
```

## Energy per engine (`power.top`)

zxvd's `power.top` is the per-engine view the activity monitor needs. It
lists the system, each app (every process of one executable) and each agent
(a model engine inside zxvd, under zxvd's app), ranked by energy. Each row
has `uj` and `mw` for the last settled interval, `source`, `state`,
`lifetime_uj` and `cpu_ns`. The figures follow "How a figure is made"
above: a meter's measured energy, split by CPU and GPU time. Nothing is
estimated. A row's `state` is `true` for metered or attributed figures,
`both` when meters disagreed (the larger reading is used), `neither` when
there was no meter and `false` when the reading was rejected. For `neither`
and `false`, `uj` and `mw` are null. A desktop PC on mains without RAPL
access, for example, lists every engine with its CPU time and `neither`.

Limits: only the companion runs a model today, so it is the only agent
noted. GPU time is not measured yet (`gpu_ns` is 0). A Linux machine
attributes by CPU time from `/proc`, so processes it may not read are left
to the system consumer.

## Temperatures (`thermal.read`)

zxvd's `thermal.read` (in the power group, always on, rules T1-T7 in
`zxv_thermal_host.h` and W9 in `zxvd_power.h`) reads this machine's
temperature sensors afresh on each call. It does not need power tracking to
be running.

| Host | Source | Tested |
|---|---|---|
| Linux | every hwmon device's `temp<N>_input`, with `temp<N>_label` and the device's `name`, under `/sys/class/hwmon`; every thermal zone's `temp` and `type` under `/sys/class/thermal` | host test on a fixture sysfs tree (`make -C kernel test-thermal`, in verify-all), the zxvd RPC test (`test_zxvd_power.sh`), the app harness (`kernel/src/apps/test_apps.sh`) and the end-to-end run (`test_zxvd.py`) |
| macOS, Apple silicon | the HID event system's temperature services (`IOHIDEventSystemClient`, usage page 0xff00, usage 5; the `Product` name is the label) | **not compiled or tested** in the Linux CI containers; it compiles only on macOS |
| macOS, Intel | SMC keys TC0P, TC0D/E/F, TG0P/D, TA0P, Tm0P, TB0T (type sp78, through IOKit's AppleSMC) | **not compiled or tested** in the Linux CI containers |
| Windows, other OSes | none | reports `neither` with the reason |

Each reading is classed `cpu`, `gpu`, `case` (board, chassis, chipset, ACPI
zone) or `other`. The label decides first (CPUTIN, Package id, Core, Tctl,
Tdie, Tccd, pACC, eACC, SOC are CPU; GPU is GPU; SYSTIN, AUXTIN, board,
ambient, case, PCH, chipset are the board), then the driver or zone type
(coretemp, k10temp, k8temp, zenpower, x86_pkg_temp, cpu\*, soc\* are CPU;
amdgpu, nouveau, radeon, i915, xe, gpu\* are GPU; acpitz, nct\*, it8\*,
w83\*, f71\*, pch_\*, board\* and the laptop vendors' platform drivers listed in
`zxv_thermal_host.h` T2 are the board).

Values are integer millidegrees Celsius and are treated as hostile input. A
value file longer than 32 bytes is refused ("too long"). A value that is not
an optional `-` and 1 to 9 digits with at most one trailing newline is "not
an integer". A value outside -50 C to +150 C is "out of range", and a file
that cannot be read is "unreadable". A refused reading stays in the list
with state `false`, its reason and `mdeg: null`. Names and labels keep
printable ASCII only. At most 64 hwmon devices, 64 zones, 512 directory
entries each and 128 readings are looked at. Readings past 128 are counted
as `dropped`.

The report's `state` is the four-valued knowledge join (`lpres_kjoin`) over
its readings. It is `true` when every reading passed, `false` when every one
was refused, `both` when some were refused, and `neither` when there is no
reading at all. Each class has its own `state`, `n` and `max_mdeg` (null
when `n` is 0). A host with no readable sensor answers `neither` with the
`reason` and an empty list. It never answers with a number that was not read.

The CI containers have no sensors: `/sys/class/hwmon` is absent and
`/sys/class/thermal` is empty. There `thermal.read` answers `neither`, "no
temperature sensor under /sys/class/hwmon or /sys/class/thermal". To see
what a machine has, run `test_zxv_thermal_host live` (built by `make -C
kernel test-thermal` into `$ZXV_TMP`, `/tmp` by default).

Apps ask for `power.status`, `power.top` and `thermal.read` with the
`power.read` capability (`kernel/src/apps/zxapp.h` R3). `sys.read` still
covers `power.status`, so apps written before `power.read` existed keep
working. That overlap is for compatibility only: `power.read` is the right
capability for power and heat.

## Not done yet

- **zxvd** serves `power.status`, `power.top`, `power.budget` and `thermal.read` (`kernel/arch/hosted/zxvd_power.c`), with agents noted around each model call and `pw_admit` run before it. See docs/ZXVD.md, "Power", for the flat `power.budget` params.
- **Temperatures on macOS.** The Apple silicon and Intel readers in `zxv_thermal_host.c` have not been compiled or run here. They need a build and a run on a Mac before they can be called working.
- **Other sensors.** SMBus, embedded-controller and INA228 readings belong to the arbiter's host sensors (`arb_host_sensor()`, another thread's work) and are not read by `thermal.read`. Fan speeds and GPU temperatures from vendor tools (no hwmon device) are not read.
- **Brazier app.** The UI (app 21, "Power, battery and heat") is not built.
- **Bare metal.** The arch layer has no fault-safe `rdmsr` yet, so RAPL is not
  wired into the boot image. There is also no ACPI battery, no thermal zones,
  no sleep states and no CPU frequency scaling. See
  `TURNKEY_OS_GAPS.md` and the OS feature audit.
- **Windows.** No battery design capacity (needs `IOCTL_BATTERY_QUERY_INFORMATION`).
  The Windows reader is compile-checked with mingw only.
