# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Structural checks of the Unreal plugin wrapper, without Unreal.

This is NOT a compile: CI has no Unreal Engine. It catches the mistakes a
text check can see:

  * ZxvUnreal.uplugin is valid JSON and each module has
    Source/<Name>/<Name>.Build.cs declaring `class <Name> : ModuleRules`;
  * every header with UCLASS / USTRUCT / UENUM includes "<stem>.generated.h"
    as its last #include, and every UCLASS / USTRUCT has GENERATED_BODY();
  * every UFUNCTION declared with `;` has a `Class::Name(` definition in a
    Private .cpp;
  * braces and parentheses balance in every C++ / C# file;
  * the engine-neutral layer (ZxvCore/) includes no Unreal header;
  * nothing includes or names a kernel/ path (public ABI only);
  * every source file starts with the Apache-2.0 header lines.

usage: python3 tools/check_ue_layout.py [KIT_DIR]
"""
import json
import os
import re
import sys

LICENSE_1 = "Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC"
LICENSE_2 = "SPDX-License-Identifier: Apache-2.0"
UE_HEADERS = re.compile(r'#include\s+"(CoreMinimal|UObject/|Engine/|Kismet/|Subsystems/|Modules/|Tickable)')


def strip(src):
    """Remove comments and string / char literals, keeping newlines."""
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * src.count("\n", i, j))
            i = j
        elif src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src[i] in "\"'":
            q, j = src[i], i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == "\\" else 1
            out.append(q + q)
            i = j + 1
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def main(kit):
    errors = []
    err = errors.append
    src_root = os.path.join(kit, "Source")

    with open(os.path.join(kit, "ZxvUnreal.uplugin"), encoding="utf-8") as f:
        try:
            plugin = json.load(f)
        except ValueError as e:
            err(f"ZxvUnreal.uplugin: not JSON: {e}")
            plugin = {"Modules": []}
    for m in plugin.get("Modules", []):
        name = m.get("Name", "")
        bcs = os.path.join(src_root, name, f"{name}.Build.cs")
        if not os.path.isfile(bcs):
            err(f"module {name}: {bcs} missing")
            continue
        with open(bcs, encoding="utf-8") as f:
            if not re.search(rf"\bclass\s+{name}\s*:\s*ModuleRules\b", f.read()):
                err(f"{bcs}: no `class {name} : ModuleRules`")
        if m.get("Type") != "Runtime":
            err(f"module {name}: Type should be Runtime so other plugins can depend on it")

    files = []
    for base in ("Source", "test", "tools"):
        for d, dirs, names in os.walk(os.path.join(kit, base)):
            dirs[:] = [x for x in dirs if x != "ThirdParty"]
            files += [os.path.join(d, x) for x in names if x.endswith((".h", ".hpp", ".cpp", ".cs", ".py", ".sh"))]
    files += [os.path.join(kit, "CMakeLists.txt")]

    cpp_defs = ""
    for p in files:
        if p.endswith(".cpp") and os.sep + "Private" + os.sep in p:
            with open(p, encoding="utf-8") as f:
                cpp_defs += strip(f.read())

    nfunc = 0
    for p in sorted(files):
        rel = os.path.relpath(p, kit)
        with open(p, encoding="utf-8") as f:
            text = f.read()
        head = "\n".join(text.splitlines()[:4])
        if LICENSE_1 not in head or LICENSE_2 not in head:
            err(f"{rel}: missing the Apache-2.0 header lines")
        if not p.endswith(".py") and (re.search(r'#include\s+[<"][^>"]*kernel/', text) or "../kernel" in text):
            err(f"{rel}: refers to kernel/ (use the public ABI only)")
        if not p.endswith((".h", ".hpp", ".cpp", ".cs")):
            continue
        code = strip(text)
        for a, b in ("{}", "()", "[]"):
            if code.count(a) != code.count(b):
                err(f"{rel}: unbalanced {a}{b} ({code.count(a)} vs {code.count(b)})")
        if os.sep + "ZxvCore" + os.sep in p and UE_HEADERS.search(text):
            err(f"{rel}: the engine-neutral layer includes an Unreal header")
        if not p.endswith(".h"):
            continue
        reflected = re.search(r"^\s*(UCLASS|USTRUCT|UENUM)\s*\(", code, re.M)
        if reflected:
            stem = os.path.splitext(os.path.basename(p))[0]
            incs = re.findall(r'^\s*#include\s+["<]([^">]+)[">]', text, re.M)
            if not incs or incs[-1] != f"{stem}.generated.h":
                err(f"{rel}: the last #include must be \"{stem}.generated.h\"")
        n_types = len(re.findall(r"^\s*(UCLASS|USTRUCT)\s*\(", code, re.M))
        n_bodies = len(re.findall(r"\bGENERATED_BODY\s*\(\s*\)", code))
        if n_types != n_bodies:
            err(f"{rel}: {n_types} UCLASS/USTRUCT but {n_bodies} GENERATED_BODY()")
        # UFUNCTION declarations: find the declaration after each macro.
        for m in re.finditer(r"UFUNCTION\s*\((?:[^()]|\([^()]*\))*\)\s*([^;{]*?)\b(\w+)\s*\(", code):
            name = m.group(2)
            nfunc += 1
            # owner: the last class declared before this point
            owner = None
            for cm in re.finditer(r"^\s*class\s+(?:\w+_API\s+)?(\w+)\s*:", code[: m.start()], re.M):
                owner = cm.group(1)
            # find the end of the declaration: matching ')' then ';' or '{'
            depth, i = 0, m.end() - 1
            while i < len(code):
                if code[i] == "(":
                    depth += 1
                elif code[i] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            tail = code[i + 1 : i + 80].lstrip()
            tail = re.sub(r"^(const|override|\s)+", "", tail)
            if tail.startswith("{"):
                continue  # defined inline
            if not re.search(rf"\b{owner}::{name}\s*\(", cpp_defs):
                err(f"{rel}: UFUNCTION {owner}::{name} has no definition in Private/*.cpp")
    if errors:
        for e in errors:
            print(e)
        print(f"check_ue_layout: FAILED ({len(errors)} problems)")
        return 1
    print(f"check_ue_layout: OK ({len(files)} files, {nfunc} UFUNCTIONs; structure only, not a compile)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..")))
