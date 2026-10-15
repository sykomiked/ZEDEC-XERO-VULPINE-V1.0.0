<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# ACPI: tables, AML interpreter, power management

On a PC, everything that makes a machine feel like a laptop lives in the
firmware's ACPI byte code (AML): how to power off and suspend, the power
button, the lid, the battery, temperatures, CPU frequency states and screen
brightness. ZXV runs that byte code with its own interpreter. It is integer
only, uses no libc and allocates from one fixed arena, and every loop has a
budget. Firmware AML is treated as untrusted input: anything malformed ends in
an error code, never a crash or a hang.

Code: `kernel/src/acpi/`. Platform glue for x86_64:
`kernel/arch/x86_64/acpi_x86.c`. Class drivers: [HW_DRIVERS.md](HW_DRIVERS.md).

| File | What it does |
|---|---|
| `acpi.c` / `acpi.h` | RSDP, XSDT (or RSDT) and table checksums. `acpi_init_from_rsdp` takes the RSDP's physical address and a map hook, so tables above the identity map work; `acpi_init` is the older BIOS-area scan. |
| `aml.h` | The interpreter's public API: load tables, look up names, evaluate methods, and typed helpers for batteries, thermal zones, the lid, processors and backlight. |
| `aml_core.c` | Byte code parser and evaluator, namespace, values, implicit conversions, the arena heap. |
| `aml_region.c` | Operation regions and fields: SystemMemory, SystemIO, PCI_Config, EmbeddedControl, SystemCMOS, table data; Index/Bank fields; the embedded controller driver. |
| `aml_dev.c` | Namespace walks, `_HID`/`_CID` matching, `_STA`, `_REG` and `_INI` start-up, and the typed helpers. |
| `acpi_pm.c` / `acpi_pm.h` | FADT registers, sleep states (`_PTS`, SLP_TYP/SLP_EN, `_WAK`), the fixed power button, GPE methods, EC queries, and the boot report. |

## At boot

`kernel_main_x86_64.c` calls `acpi_x86_boot` right after the class drivers,
before `[BOOT_OK]`:

1. **RSDP.** The UEFI stub records the RSDP from the firmware's
   configuration table (ACPI 2.0 GUID first, then 1.0) in the handoff v2
   record (`kernel/arch/zxv_handoff.h`). The kernel uses it only when the
   record is accepted. If the record is damaged, or says the kernel window
   overwrote the ACPI tables or ACPI NVS memory, ACPI is held: no AML runs,
   so there is no sleep or power button on that boot, and the boot logs an
   `[ACPI HELD]` line. A direct `-kernel` boot (no record) falls back to the
   BIOS-area scan.
2. **Tables.** XSDT when the RSDP is revision 2 or later and its extended
   checksum is good, else RSDT. A table with a bad checksum is skipped.
3. **Namespace.** The DSDT (`X_DSDT` first), then every SSDT and PSDT. Errors
   inside a table are logged and the rest of the table still loads, as ACPICA
   does. Then `_REG` for PCI_Config, CMOS and EmbeddedControl regions,
   `\_SB._INI` and every present device's `_INI` (children of an absent
   device are skipped unless it reports "functioning").
4. **ACPI mode.** If SCI_EN is clear, `ACPI_ENABLE` goes to `SMI_CMD`; the
   fixed power button is enabled in PM1.
5. **Report**, one line each:

```
[ACPI OK] namespace: 259 objects, 75 methods, DSDT rev 1 (1 tables, 0 load errors, 1 _INI, heap 94 KB)
[ACPI] S3 supported SLP_TYPa=0x1 SLP_TYPb=0x1
[ACPI] \_S5_ SLP_TYPa=0x0 SLP_TYPb=0x0
[ACPI] devices with _HID: 29 (29 present): PNP0A08, PNP0A06 x3, QEMU0002, PNP0303, PNP0F13, PNP0400, PNP0501, PNP0B00, PNP0C01, PNP0C0F x16, ...
[ACPI] power button: fixed feature (PM1 PWRBTN), control-method devices PNP0C0C: 0, GPE methods: 2
[ACPI] battery PNP0C0A: 0
[ACPI] lid PNP0C0D: 0
[ACPI] thermal zones: 0
[ACPI] processors: 2 (Processor() objects 2), _PSS states 0 on #0 (0 with _PSS), _CPC entries 0 (0 with _CPC)
[ACPI] backlight: 0 output(s) with _BCL, 0 levels
[ACPI] embedded controller: none, ACPI mode on
```

(That is QEMU q35 with `-smp 2`.) The boot never suspends or powers off on
its own; a failure anywhere is logged and the boot goes on.

## Interpreter

- **Coverage.** All of ACPI 6.5's AML opcodes: data objects, Name/Alias/
  Scope/Device/Processor/PowerResource/ThermalZone/Method, every Field kind
  with AccessAs/Offset and the three update rules, Create*Field, the integer,
  logical, string and buffer operators with ACPI's implicit conversions,
  Package/VarPackage, Index/RefOf/DerefOf/CondRefOf/ObjectType/SizeOf,
  If/Else/While/Break/Continue/Return, Mutex/Event (single-threaded:
  Acquire never blocks), Notify, Sleep/Stall, Timer, Fatal, Load
  (from a region or buffer; a table already loaded is refused) and Debug.
  `LoadTable` returns failure (Zero). `_OSI` answers the Windows strings
  firmware tests for, so the methods take their modern paths.
- **32/64-bit integers.** A revision 1 DSDT makes every integer 32 bits.
- **Forward references.** A first pass declares methods and containers, so
  module-level code may call a method defined later in the table; `External`
  records argument counts for names that live in another table.
- **Values.** Every object has one owner and is copied on store. References
  (RefOf, Index, names in packages) name a location plus a generation, so a
  reference to a deleted object or a returned method's local reads as an
  error instead of freed memory.
- **Budgets** (`aml.h`): 24 nested calls, 192 levels of evaluator recursion,
  0x10000 iterations per While, 4 M opcodes per top-level call (a caller can
  lower it with `op_limit`), 3 s of Sleep/Stall, 256 KB per string or buffer,
  4096 elements per package, 64 skipped bad opcodes per table.
- **Regions.** SystemMemory through the platform's `mmio_map` (a misaligned
  unit is done byte by byte), SystemIO, PCI_Config (address from `_ADR`, the
  root bridge's `_SEG`/`_BBN` and each PCI-to-PCI bridge's secondary bus),
  EmbeddedControl (RD/WR/QR commands with IBF/OBF polling, a 20 ms timeout per
  step, and the EC is given up after 8 timeouts), SystemCMOS, and
  DataTableRegion. SMBus, GPIO, GenericSerialBus, IPMI and PCC report
  `AML_EUNSUP`. A region's address and length are evaluated at first use.
- **`_STA` as a verdict.** `aml_sta_verdict` maps `_STA` to the four-valued
  `hw_verdict_t`: present and functioning = T, present only = F, functioning
  but not present = B (both), neither = N.

## Power management

- **Sleep.** `acpi_enter_sleep(pm, S)` runs `_PTS(S)` and `\_SI._SST`, writes
  SLP_TYPa/SLP_TYPb to PM1a/PM1b control, flushes caches (`wbinvd`), then sets
  SLP_EN; on hardware-reduced platforms it uses the sleep control register.
  It then waits up to 1 s for WAK_STS and runs `_WAK(S)`. `acpi_poweroff` is
  S5.
- **Events.** `acpi_poll_events` checks the fixed power button, runs `_Lxx`
  (level: run, then clear) and `_Exx` (edge: clear, then run) for enabled
  GPEs, queries the EC on its GPE and runs `_Qxx`, and drains Notify()
  (0x80 on a PNP0C0C device is a power button press, on PNP0C0D a lid
  change). There is no SCI interrupt handler yet: the kernel runs with
  interrupts off, so events are only seen while something polls.
- **Global lock.** Fields declared `Lock` take the FACS global lock with the
  specification's cmpxchg sequence and signal GBL_RLS when firmware waits.

## Testing

**Host** (`make -C kernel test-aml`, part of `verify-all`; ASan + UBSan):

| Test | Checks |
|---|---|
| `test_aml_ops` | every operator, conversions, control flow, references, packages, method-local objects, notifications, budgets |
| `test_aml_regions` | each address space on a fake chipset: memory, ports, PCI (bridges and `_BBN`), EC with a query and a dead EC, CMOS, index and bank fields, Load() from a region |
| `test_aml_pm` | RSDP/XSDT/RSDT discovery and bad checksums, FADT parsing, `_INI` rules, batteries (`_BIX` and `_BIF`), lid, thermal zones, `_PSS`/`_PCT`/`_PPC`/`_CPC`, backlight, S3/S5 register writes, hardware-reduced sleep, power button, GPEs, EC queries, the report lines |
| `test_aml_hostile` | hand-assembled hostile byte code (endless loops, recursion, 4 GB buffers and packages, 400-deep expressions, 300-deep packages, truncated lengths, bad names, unknown opcodes, duplicate names), a small arena, and a deterministic mutation fuzz of every fixture (300 rounds per fixture in CI; `AML_FUZZ_ITERS` and `AML_FUZZ_SEED` run more) |

The fixtures are ASL in `kernel/src/acpi/test/fixtures/`, compiled once with
iasl into `aml_fixtures.h`, which is committed, so CI never needs iasl. After
editing an `.asl` file run `bash kernel/src/acpi/test/gen_fixtures.sh` (needs
`acpica-tools`). Constant folding is off (`-oa`), so the opcodes in the ASL are
the opcodes the interpreter runs.

**QEMU** (`build_system/acpi_check.sh`, CI step "ACPI power management on
emulated hardware"): q35 + OVMF, three boots selected by fw_cfg files that the
kernel reads only when the ACPI OEM is QEMU's and the fw_cfg signature is
present:

1. the report must show `[ACPI OK]`, `\_S5_`, the S3 line, the power button
   and two processors; the kernel then polls the power button, the monitor's
   `system_powerdown` presses it, and QEMU must exit through S5;
2. `opt/zxv/acpi_poweroff=1`: S5 straight after the report, QEMU must exit;
3. `opt/zxv/acpi_s3=1` with `ICH9-LPC.disable_s3=0`: the monitor must report
   the guest suspended after the kernel enters S3.

**Mutation** (`tests/harness/mutator/mutate.py --config
tests/harness/mutator/targets.json --only aml_core`): the interpreter core
against all four host tests (`kernel/src/acpi/test/mut_aml.sh`).

## Not done yet

- **S3 resume.** S3 entry works (QEMU reports the guest suspended), but the
  kernel sets no FACS waking vector, so a wake does not return into ZXV. A
  resume path needs a real-mode trampoline that rebuilds long mode and
  restores device state.
- **Batteries, lid, thermal zones, `_PSS`/`_CPC` and backlight** are tested on
  the host only. QEMU 8.2's q35 tables have none of them, so the boot report
  shows zero counts there; no physical laptop has run this code yet.
- **No SCI interrupt.** Power button, GPEs and EC queries are polled.
- **No `_OSC`/`_PDC` handshake, no `_PRT` interrupt routing, no S4, no
  `_PTS`-time device power states (`_PS0`-`_PS3`, `_PR0`).**
- Region spaces SMBus, GPIO, GenericSerialBus, IPMI and PCC are not served;
  PCI_Config reaches segment 0 through port CF8/CFC only (no ECAM).
