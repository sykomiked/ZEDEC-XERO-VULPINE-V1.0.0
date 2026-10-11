#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC
# SPDX-License-Identifier: Apache-2.0
"""Check that hardware design files and the ZXV Hardware License stay inside
hardware/, and that nothing inside hardware/ claims Apache-2.0."""
import pathlib, subprocess, sys

HW_EXT = {".v", ".sv", ".svh", ".vh", ".vhd", ".vhdl", ".gds", ".oas",
          ".kicad_sch", ".kicad_pcb", ".sch", ".brd", ".gbr", ".step", ".stp"}
HW_ID = "LicenseRef-ZXV-Hardware-1.0"
# Published before the split; placement decided separately.
ALLOW_OUTSIDE = {"kernel/src/plnp/plnp_frame_parser.sv"}
LICENCE_COPIES = {"LICENSES/LicenseRef-ZXV-Hardware-1.0.txt", "hardware/LICENSE",
                  "LICENSE-HARDWARE.md", "REUSE.toml", "tools/check_licence_split.py"}

files = subprocess.run(["git", "ls-files"], capture_output=True, text=True,
                       check=True).stdout.split("\n")
bad = []
for f in filter(None, files):
    p = pathlib.Path(f)
    inside = f.startswith("hardware/")
    if not inside and p.suffix.lower() in HW_EXT and f not in ALLOW_OUTSIDE:
        bad.append(f"{f}: hardware design file outside hardware/")
    try:
        head = p.read_text(errors="ignore")[:2000]
    except OSError:
        continue
    if inside and "SPDX-License-Identifier: Apache-2.0" in head:
        bad.append(f"{f}: Apache-2.0 header inside hardware/")
    if not inside and f not in LICENCE_COPIES and f"SPDX-License-Identifier: {HW_ID}" in head:
        bad.append(f"{f}: hardware licence header outside hardware/")
if bad:
    print("\n".join(bad)); sys.exit(1)
print("licence split OK")
