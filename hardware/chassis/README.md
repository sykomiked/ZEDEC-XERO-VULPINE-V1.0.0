<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Chassis drawings

| File | What it is |
|---|---|
| [gen_grilles.py](gen_grilles.py) | Generates the honeycomb window drawings from the numbers in [../docs/oem/HARDENING.md](../docs/oem/HARDENING.md) section 6 |
| [grilles/](grilles/) | One SVG per device, in millimetres: Signet back, Folio back, Courier lid, briefcase vault window, Hearth door, M and L rack shell doors |

Each drawing shows the outline, the clear window, the forest-night
`#002911` hexagonal grille, the emblem keep-out at the centre where the
backlit ILLUMAHEART FOUNDATION badge sits, and a title block with the
dimensions. Change a dimension in `gen_grilles.py` and run it again:

    python3 hardware/chassis/gen_grilles.py

The emblem itself is shown as its keep-out and size; its vector master is
a separate deliverable ([../docs/oem/HARDENING.md](../docs/oem/HARDENING.md)
section 6.5). The drawings are design intent for the industrial-design
and tooling vendors, not toleranced production drawings.
