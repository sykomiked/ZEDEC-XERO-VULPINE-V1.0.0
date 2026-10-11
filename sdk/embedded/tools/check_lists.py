# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Check that the Zephyr module compiles the same libzxv/kernel sources as
sources.mk (the Make builds), so the two cannot drift apart.

usage: python3 tools/check_lists.py   (from sdk/embedded)
"""
import re
import sys


def make_list():
    s = open("sources.mk", encoding="utf-8").read()
    libzxv = re.search(r"ZXE_LIBZXV_SRC := \$\(addprefix \$\(ZXE_LIBZXV\)/src/,(.*?)\)\n", s, re.S).group(1)
    kern = re.search(r"ZXE_KSRC := \$\(addprefix \$\(ZXE_K\)/src/,(.*?)\)\n", s, re.S).group(1)
    files = ["libzxv/src/" + f for f in libzxv.replace("\\", " ").split()]
    files += ["kernel/src/" + f for f in kern.replace("\\", " ").split()]
    return sorted(files)


def cmake_list():
    s = open("zephyr-zxv/CMakeLists.txt", encoding="utf-8").read()
    block = s[s.index("# zxe-sources-begin"):s.index("# zxe-sources-end")]
    out = []
    for m in re.finditer(r"\$\{ZXE_LIBZXV\}/(src/\S+\.c)|\$\{ZXE_K\}/(src/\S+\.c)", block):
        out.append("libzxv/" + m.group(1) if m.group(1) else "kernel/" + m.group(2))
    return sorted(out)


def main():
    a, b = make_list(), cmake_list()
    if a != b:
        print("sources.mk and zephyr-zxv/CMakeLists.txt differ:")
        for f in sorted(set(a) ^ set(b)):
            print("  %s only in %s" % (f, "sources.mk" if f in a else "CMakeLists.txt"))
        return 1
    print("check_lists: %d sources agree (sources.mk, zephyr-zxv/CMakeLists.txt)" % len(a))
    return 0


if __name__ == "__main__":
    sys.exit(main())
