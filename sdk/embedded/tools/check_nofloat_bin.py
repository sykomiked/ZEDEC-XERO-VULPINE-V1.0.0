# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Binary no-float check of the cross-built embedded subset.

Every cross object is compiled with a soft-float ABI (-mfloat-abi=soft on
Arm, rv32imac/ilp32 on RISC-V), so the compiler cannot emit an FPU
instruction; any floating-point arithmetic shows up as a call to a libgcc
soft-float helper (__aeabi_dadd, __adddf3, __floatsidf, ...). This script
lists, per object, the helpers it references (nm -u) and fails if any
object outside the allowed set does.

Allowed, ZXE_PROFILE=exact only: zxv_api.o and triple_ledger.o, the kernel
triple-ledger mirror built with -DTEST_HOST exactly as in the hosted
libzxv (double arithmetic on integers <= 2^53, shadowed by the exact int64
rails and cross-checked by zxv_ledger_check). Nothing is allowed in
ZXE_PROFILE=q32, and nothing ever in the port layer or zxe.

It also reports, for information, the 64-bit division helpers each object
calls and fails if a ZXP_NO_TLS build still references the thread pointer
(__aeabi_read_tp / __emutls).

usage: python3 tools/check_nofloat_bin.py [--profile exact|q32] BUILD_DIR TARGET...
"""
import glob
import os
import re
import subprocess
import sys

FLOAT = re.compile(r"^(__aeabi_[df]\w*|__aeabi_u?[il]2[df]|__(add|sub|mul|div|neg)[sdt]f3|"
                   r"__(eq|ne|lt|le|gt|ge|unord|cmp)[sdt]f2|__float(un)?[sdt]i[sdt]f|"
                   r"__fix(uns)?[sdt]f[sdt]i|__(extend|trunc)\w*f2)$")
DIV64 = re.compile(r"^(__aeabi_u?ldivmod|__(u)?(div|mod)di3|__udivmoddi4)$")
TLS = re.compile(r"^(__aeabi_read_tp|__emutls_\w+|__tls_get_addr)$")
ALLOWED = {"exact": {"zxv_api.o", "triple_ledger.o"}, "q32": set()}
NM = {"rv32imac": "riscv64-unknown-elf-nm"}


def undefined(nm, obj):
    out = subprocess.run([nm, "-u", obj], capture_output=True, text=True, check=True).stdout
    return {line.split()[-1] for line in out.splitlines() if line.strip()}


def main(argv):
    profile = "exact"
    if argv[:1] == ["--profile"]:
        profile, argv = argv[1], argv[2:]
    out, targets = argv[0], argv[1:]
    bad = 0
    for t in targets:
        nm = NM.get(t, "arm-none-eabi-nm")
        objs = sorted(glob.glob(os.path.join(out, t, "*", "*.o")))
        if not objs:
            print("%s: no objects (run make size first)" % t)
            bad += 1
            continue
        floats, divs = [], []
        for o in objs:
            u = undefined(nm, o)
            f = sorted(s for s in u if FLOAT.match(s))
            d = sorted(s for s in u if DIV64.match(s))
            tls = sorted(s for s in u if TLS.match(s))
            name = os.path.basename(o)
            where = os.path.basename(os.path.dirname(o))
            if f:
                ok = where == "zxv" and name in ALLOWED[profile]
                floats.append("%s/%s: %s%s" % (where, name, " ".join(f), "" if ok else "   <-- NOT ALLOWED"))
                bad += 0 if ok else 1
            if d:
                divs.append("%s/%s" % (where, name))
            if tls:
                print("%s: %s/%s references the thread pointer (%s) in a ZXP_NO_TLS build"
                      % (t, where, name, " ".join(tls)))
                bad += 1
        print("%s (%d objects, profile %s)" % (t, len(objs), profile))
        print("  soft-float helper calls: %s" % ("none" if not floats else ""))
        for line in floats:
            print("    " + line)
        print("  64-bit division helpers (libgcc, not constant-time): %s" % (", ".join(divs) or "none"))
    if bad:
        print("check_nofloat_bin: %d problem(s)" % bad)
        return 1
    print("check_nofloat_bin: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
