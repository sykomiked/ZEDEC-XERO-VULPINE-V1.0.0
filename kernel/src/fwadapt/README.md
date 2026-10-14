<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# fwadapt: the smart firmware adapter

The adapter decides which driver, if any, may touch each device. It gathers
evidence from ACPI, the device tree, PCI and USB IDs, and read-only probes. It
combines those claims with lpres's knowledge join and acts on the resulting
hexagram tag:

| Evidence | Decision |
|---|---|
| a probe faulted (forbidden) | **quarantine**: no driver, and the claims are recorded |
| sources conflict (both) | **safe generic** class driver if the class itself is agreed, otherwise **disabled**; always flagged for a person |
| IDs agree (true) | **specific** driver from the table |
| only the class is known | **class generic**, but only for standard interfaces (xHCI, NVMe, AHCI, HDA, USB HID, ...) |
| nothing known (neither) | **none**: no register is ever guessed |
| agreeing but stale evidence | **defer**: probe again first |

A driver that the table knows but this image does not contain is reported as
**unavailable**. `fwa_to_hwp` writes the decisions into a ZHP-1 hardware
profile, so the emulation matrix can see what is served natively.

The adapter cannot make truly unknown hardware work without a spec or a
driver. What it does is pick the right driver and fail safe. The source is
freestanding C11 and is tested by `make test-fwadapt` in `kernel/`.
