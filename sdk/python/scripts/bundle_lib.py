# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Copy a built libzxv into src/zxv/_lib/ so the wheel bundles it.

Usage: python scripts/bundle_lib.py [path/to/libzxv.so]
(default: ../libzxv/build/libzxv.{so,dylib} or zxv.dll)
"""

import shutil
import sys
from pathlib import Path

here = Path(__file__).resolve().parent.parent
dest = here / "src" / "zxv" / "_lib"
if len(sys.argv) > 1:
    src = Path(sys.argv[1])
else:
    build = here.parent / "libzxv" / "build"
    found = [build / n for n in ("libzxv.so", "libzxv.dylib", "zxv.dll") if (build / n).exists()]
    if not found:
        sys.exit(f"no libzxv in {build}; build it with make -C sdk/libzxv first")
    src = found[0]
dest.mkdir(parents=True, exist_ok=True)
shutil.copy2(src, dest / src.name)
print(f"bundled {src} -> {dest / src.name}")
