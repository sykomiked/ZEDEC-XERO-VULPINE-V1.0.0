#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Check that every SDK manifest declares the version being released.

  python3 sdk/release/check_versions.py                 # version = zxv_api.h
  python3 sdk/release/check_versions.py --version 1.3.0
  python3 sdk/release/check_versions.py --version 1.3.0 --strict

Standard library only (Python 3.9+). Exit status 0 when the release can go
ahead, 1 when something disagrees, 2 on a usage error.

What is checked (see POLICY below; move a row to change the policy):

  lockstep     must equal the release version. These are the language and
               mobile SDKs that wrap libzxv one-to-one, plus zxv_api.h.
  pin          a dependency on a lockstep package must accept the release
               version (for example the Spring starter's zxv.version).
  independent  versioned on their own line (framework kits, engine kits,
               plugin kits). Reported; a mismatch fails only with --strict.
  tag          no version in the manifest; the version is the git tag
               (Go, SwiftPM, Composer). Reported for information.

It also checks that sdk/libzxv/abi/libzxv-<X.Y.Z>.json exists: every
published release records its ABI baseline (sdk/docs/STABILITY.md).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


# ---------------------------------------------------------------- readers
# Each reader returns the declared string, or raises LookupError with a
# human-readable reason.

def _text(path: str) -> str:
    p = ROOT / path
    if not p.is_file():
        raise LookupError(f"{path} not found")
    return p.read_text(encoding="utf-8")


def regex(path: str, pattern: str, flags: int = re.M):
    def read() -> str:
        m = re.search(pattern, _text(path), flags)
        if not m:
            raise LookupError(f"no match for /{pattern}/ in {path}")
        return m.group(1)
    return read


def toml_key(path: str, section: str, key: str):
    """A top-level `key = "..."` inside [section] (enough for our manifests)."""
    def read() -> str:
        cur = None
        for line in _text(path).splitlines():
            s = line.strip()
            m = re.match(r"^\[\[?([^\]]+)\]\]?$", s)
            if m:
                cur = m.group(1).strip()
                continue
            if cur == section:
                m = re.match(rf'^{re.escape(key)}\s*=\s*"([^"]*)"', s)
                if m:
                    return m.group(1)
        raise LookupError(f"no {key} in [{section}] of {path}")
    return read


def toml_dep_version(path: str, section: str, dep: str):
    """`dep = { ..., version = "..." }` inside [section]."""
    def read() -> str:
        cur = None
        for line in _text(path).splitlines():
            s = line.strip()
            m = re.match(r"^\[([^\]]+)\]$", s)
            if m:
                cur = m.group(1).strip()
                continue
            if cur == section and re.match(rf"^{re.escape(dep)}\s*=", s):
                m = re.search(r'version\s*=\s*"([^"]*)"', s)
                if m:
                    return m.group(1)
                m = re.match(rf'^{re.escape(dep)}\s*=\s*"([^"]*)"', s)
                if m:
                    return m.group(1)
        raise LookupError(f"no {dep} version in [{section}] of {path}")
    return read


def toml_array_item(path: str, section: str, key: str, prefix: str):
    """The entry of `key = [...]` in [section] that starts with prefix."""
    def read() -> str:
        cur, buf, grab = None, "", False
        for line in _text(path).splitlines():
            s = line.strip()
            m = re.match(r"^\[([^\]]+)\]$", s)
            if m and not grab:
                cur = m.group(1).strip()
                continue
            if cur == section and (grab or re.match(rf"^{re.escape(key)}\s*=", s)):
                buf += s
                grab = "]" not in s
                if not grab:
                    for item in re.findall(r'"([^"]*)"', buf):
                        if item.replace(" ", "").startswith(prefix):
                            return item.replace(" ", "")[len(prefix):]
                    break
        raise LookupError(f"no {prefix}... in {key} of [{section}] in {path}")
    return read


def json_key(path: str, *keys: str):
    def read() -> str:
        try:
            v = json.loads(_text(path))
        except json.JSONDecodeError as e:
            raise LookupError(f"{path}: invalid JSON ({e})") from None
        for k in keys:
            if not isinstance(v, dict) or k not in v:
                raise LookupError(f"no {'.'.join(keys)} in {path}")
            v = v[k]
        return str(v)
    return read


def xml_text(path: str, xpath: str):
    """ElementTree path with the default namespace written as 'm:'."""
    def read() -> str:
        try:
            root = ET.fromstring(_text(path))
        except ET.ParseError as e:
            raise LookupError(f"{path}: invalid XML ({e})") from None
        ns = {}
        m = re.match(r"\{([^}]*)\}", root.tag)
        if m:
            ns["m"] = m.group(1)
            q = xpath
        else:
            q = xpath.replace("m:", "")
        el = root.find(q, ns)
        if el is None or not (el.text or "").strip():
            raise LookupError(f"no {xpath.replace('m:', '')} in {path}")
        return el.text.strip()
    return read


def header_version(path: str, prefix: str):
    def read() -> str:
        src = _text(path)
        parts = []
        for k in ("MAJOR", "MINOR", "PATCH"):
            m = re.search(rf"^\s*#\s*define\s+{prefix}{k}\s+(\d+)", src, re.M)
            if not m:
                raise LookupError(f"no {prefix}{k} in {path}")
            parts.append(m.group(1))
        return ".".join(parts)
    return read


# ------------------------------------------------------------ comparisons

def core(v: str) -> str:
    m = SEMVER.match(v)
    return ".".join(m.group(i) for i in (1, 2, 3)) if m else v


_PEP440_PRE = {"a": "a", "alpha": "a", "b": "b", "beta": "b", "c": "rc", "rc": "rc",
               "pre": "rc", "preview": "rc"}


def pep440(v: str) -> str:
    """1.3.0-rc.1 / 1.3.0rc1 / 1.3.0.rc1 -> 1.3.0rc1 (PEP 440 normal form)."""
    m = re.match(r"^(\d+\.\d+\.\d+)(?:[-_.]?([A-Za-z]+)[-_.]?(\d*))?$", v.strip())
    if not m:
        return v.strip().lower()
    base, tag, num = m.groups()
    if not tag:
        return base
    tag = _PEP440_PRE.get(tag.lower(), tag.lower())
    return f"{base}{tag}{num or '0'}"


def rubygems(v: str) -> str:
    """RubyGems reads 1.3.0-rc.1 as 1.3.0.pre.rc.1."""
    return v.strip().replace("-", ".pre.").lower()


def same(kind: str, declared: str, want: str) -> bool:
    if kind == "semver":
        return declared.strip() == want
    if kind == "semver-ci":  # NuGet compares case-insensitively
        return declared.strip().lower() == want.lower()
    if kind == "core":  # header and CMake carry only X.Y.Z
        return declared.strip() == core(want)
    if kind == "pep440":
        return pep440(declared) == pep440(want)
    if kind == "rubygems":
        return rubygems(declared) == rubygems(want)
    raise ValueError(kind)


def _tuple(v: str):
    m = SEMVER.match(v)
    if not m:
        return None
    return tuple(int(m.group(i)) for i in (1, 2, 3)), m.group(4)


def _cmp(a: str, b: str) -> int:
    """Semver precedence, enough for range checks (prerelease < release)."""
    ta, tb = _tuple(a), _tuple(b)
    if ta is None or tb is None:
        raise ValueError(f"not semver: {a!r} or {b!r}")
    if ta[0] != tb[0]:
        return -1 if ta[0] < tb[0] else 1
    pa, pb = ta[1], tb[1]
    if pa == pb:
        return 0
    if pa is None:
        return 1
    if pb is None:
        return -1
    return -1 if pa < pb else 1


def _pad(v: str) -> str:
    m = re.match(r"^(\d+)(?:\.(\d+))?(?:\.(\d+))?(-.*)?$", v)
    if not m:
        return v
    return f"{m.group(1)}.{m.group(2) or 0}.{m.group(3) or 0}{m.group(4) or ''}"


def accepts(kind: str, spec: str, want: str) -> bool:
    """Does a dependency requirement accept the release version?"""
    spec = spec.strip()
    if kind == "exact":
        return spec == want
    if kind == "npm":  # ^x.y.z, ~x.y.z, >=x.y.z, x.y.z
        m = re.match(r"^(\^|~|>=)?\s*v?([\d.]+(?:-[\w.]+)?)$", spec)
        if not m:
            return False
        op, base = m.group(1) or "", _pad(m.group(2))
        if _cmp(want, base) < 0:
            return False
        bt = _tuple(base)[0]
        wt = _tuple(want)[0]
        if op == "^":
            if bt[0] > 0:
                return wt[0] == bt[0]
            return wt[:2] == bt[:2]
        if op == "~":
            return wt[:2] == bt[:2]
        if op == ">=":
            return True
        return _cmp(want, base) == 0
    if kind == "pep508":  # comma-separated >=, ==, <, ~= clauses
        w = pep440(want)
        ok = True
        for clause in spec.split(","):
            m = re.match(r"^\s*(>=|<=|==|<|>|~=)\s*([\w.]+)\s*$", clause)
            if not m:
                return False
            op, b = m.group(1), _pad(m.group(2))
            c = _cmp(core(want), core(b))
            if op == ">=":
                ok &= c >= 0
            elif op == ">":
                ok &= c > 0
            elif op == "<=":
                ok &= c <= 0
            elif op == "<":
                ok &= c < 0
            elif op == "==":
                ok &= w == pep440(b)
            elif op == "~=":
                ok &= c >= 0 and _tuple(core(want))[0][0] == _tuple(b)[0][0]
        return ok
    raise ValueError(kind)


# ----------------------------------------------------------------- policy
# (group, what, file, reader, comparison)

POLICY = [
    # libzxv itself: the ABI version macros in the one public header.
    ("lockstep", "libzxv (zxv_api.h ZXV_ABI_VERSION_*)", "sdk/libzxv/include/zxv_api.h",
     header_version("sdk/libzxv/include/zxv_api.h", "ZXV_ABI_VERSION_"), "core"),
    ("lockstep", "Python zxv-sdk [project].version", "sdk/python/pyproject.toml",
     toml_key("sdk/python/pyproject.toml", "project", "version"), "pep440"),
    ("lockstep", "Python zxv.__version__", "sdk/python/src/zxv/__init__.py",
     regex("sdk/python/src/zxv/__init__.py", r'^__version__\s*=\s*"([^"]+)"'), "pep440"),
    ("lockstep", "Java org.zedec:zxv", "sdk/java/pom.xml",
     xml_text("sdk/java/pom.xml", "m:version"), "semver"),
    ("lockstep", ".NET Zxv.Native / Zxv.Sdk / Zxv.AspNetCore <Version>", "sdk/dotnet/Directory.Build.props",
     xml_text("sdk/dotnet/Directory.Build.props", ".//Version"), "semver-ci"),
    ("lockstep", "npm @zedec/zxv", "sdk/typescript/package.json",
     json_key("sdk/typescript/package.json", "version"), "semver"),
    ("lockstep", "Rust zxv + zxv-sys [workspace.package].version", "sdk/rust/Cargo.toml",
     toml_key("sdk/rust/Cargo.toml", "workspace.package", "version"), "semver"),
    ("lockstep", "Ruby gem zxv (Zxv::VERSION)", "sdk/ruby/lib/zxv/version.rb",
     regex("sdk/ruby/lib/zxv/version.rb", r'VERSION\s*=\s*"([^"]+)"'), "rubygems"),
    ("lockstep", "C++ zxv.hpp CMake project VERSION", "sdk/cpp/CMakeLists.txt",
     regex("sdk/cpp/CMakeLists.txt", r"^\s*project\s*\(\s*zxv_cpp\s+VERSION\s+([\d.]+)"), "core"),
    ("lockstep", "Kotlin org.zedec:zxv (JVM, :zxv)", "sdk/mobile/kotlin/zxv/build.gradle.kts",
     regex("sdk/mobile/kotlin/zxv/build.gradle.kts", r'^version\s*=\s*"([^"]+)"'), "semver"),
    ("lockstep", "Dart zxv", "sdk/mobile/dart/pubspec.yaml",
     regex("sdk/mobile/dart/pubspec.yaml", r"^version:\s*['\"]?([^\s'\"#]+)"), "semver"),

    # Dependencies on lockstep packages must accept the release version.
    ("pin", "Rust zxv -> zxv-sys dependency", "sdk/rust/zxv/Cargo.toml",
     toml_dep_version("sdk/rust/zxv/Cargo.toml", "dependencies", "zxv-sys"), "exact"),
    ("pin", "Spring starter -> org.zedec:zxv (zxv.version)", "sdk/frameworks/spring-zxv/pom.xml",
     xml_text("sdk/frameworks/spring-zxv/pom.xml", "m:properties/m:zxv.version"), "exact"),
    ("pin", "NestJS kit -> @zedec/zxv (peerDependencies)", "sdk/frameworks/nest-zxv/package.json",
     json_key("sdk/frameworks/nest-zxv/package.json", "peerDependencies", "@zedec/zxv"), "npm"),
    ("pin", "django-zxv -> zxv-sdk (dependencies)", "sdk/frameworks/django-zxv/pyproject.toml",
     toml_array_item("sdk/frameworks/django-zxv/pyproject.toml", "project", "dependencies", "zxv-sdk"),
     "pep508"),

    # Own version lines. Reported; fail only with --strict.
    ("independent", "Spring parent org.zedec:spring-zxv", "sdk/frameworks/spring-zxv/pom.xml",
     xml_text("sdk/frameworks/spring-zxv/pom.xml", "m:version"), "semver"),
    ("independent", "Spring starter (inherits parent)", "sdk/frameworks/spring-zxv/zxv-spring-boot-starter/pom.xml",
     xml_text("sdk/frameworks/spring-zxv/zxv-spring-boot-starter/pom.xml", "m:parent/m:version"), "semver"),
    ("independent", "django-zxv", "sdk/frameworks/django-zxv/pyproject.toml",
     toml_key("sdk/frameworks/django-zxv/pyproject.toml", "project", "version"), "pep440"),
    ("independent", "django_zxv.__version__", "sdk/frameworks/django-zxv/src/django_zxv/__init__.py",
     regex("sdk/frameworks/django-zxv/src/django_zxv/__init__.py", r'^__version__\s*=\s*"([^"]+)"'),
     "pep440"),
    ("independent", "npm @zedec/zxv-nestjs", "sdk/frameworks/nest-zxv/package.json",
     json_key("sdk/frameworks/nest-zxv/package.json", "version"), "semver"),
    ("independent", "Unity package com.zedec.zxv", "sdk/engines/unity-zxv/com.zedec.zxv/package.json",
     json_key("sdk/engines/unity-zxv/com.zedec.zxv/package.json", "version"), "semver"),
    ("independent", "Kit Z-Wasm (ZW_VERSION_*)", "sdk/kits/z-wasm/include/zwasm.h",
     header_version("sdk/kits/z-wasm/include/zwasm.h", "ZW_VERSION_"), "core"),
    ("independent", "Kit z-contract (ZC_VERSION_*)", "sdk/kits/z-contract/include/zcontract.h",
     header_version("sdk/kits/z-contract/include/zcontract.h", "ZC_VERSION_"), "core"),
    ("independent", "Kit z-ffi (ZF_VERSION_*)", "sdk/kits/z-ffi/include/zffi.h",
     header_version("sdk/kits/z-ffi/include/zffi.h", "ZF_VERSION_"), "core"),
    ("independent", "Kit adapter-matrix (ZA_VERSION_*)", "sdk/kits/adapter-matrix/include/zadapter.h",
     header_version("sdk/kits/adapter-matrix/include/zadapter.h", "ZA_VERSION_"), "core"),
    ("independent", "Kit z-audit (ZAU_VERSION_*)", "sdk/kits/z-audit/include/zaudit.h",
     header_version("sdk/kits/z-audit/include/zaudit.h", "ZAU_VERSION_"), "core"),
    ("independent", "Kit Tri-Isolate (TI_VERSION_*)", "sdk/kits/tri-isolate/include/triisolate.h",
     header_version("sdk/kits/tri-isolate/include/triisolate.h", "TI_VERSION_"), "core"),

    # Version comes from the git tag, not the manifest.
    ("tag", "Go module .../sdk/go/zxv (tag sdk/go/zxv/vX.Y.Z)", "sdk/go/zxv/go.mod", None, None),
    ("tag", "SwiftPM Zxv (Package.swift is in sdk/swift, not the repo root)", "sdk/swift/Package.swift",
     None, None),
    ("tag", "Composer zedec/zxv (no version field)", "sdk/php/composer.json", None, None),
    ("tag", "Android AAR :zxv-android (no version set)", "sdk/mobile/kotlin/android/build.gradle.kts",
     None, None),
]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--version", help="release version, X.Y.Z[-pre] (default: zxv_api.h)")
    ap.add_argument("--strict", action="store_true",
                    help="independently versioned packages must match too")
    ap.add_argument("--no-baseline", action="store_true",
                    help="do not require sdk/libzxv/abi/libzxv-<X.Y.Z>.json")
    args = ap.parse_args(argv)

    want = args.version
    if want is None:
        try:
            want = header_version("sdk/libzxv/include/zxv_api.h", "ZXV_ABI_VERSION_")()
        except LookupError as e:
            print(f"error: {e}", file=sys.stderr)
            return 2
    if want.startswith("v"):
        want = want[1:]
    if not SEMVER.match(want):
        print(f"error: {want!r} is not a version of the form X.Y.Z or X.Y.Z-pre", file=sys.stderr)
        return 2

    print(f"Release version: {want}" + ("  (prerelease)" if (SEMVER.match(want).group(4)
                                                             or want.startswith("0.")) else ""))
    print()
    failures: list[str] = []
    width = max(len(w) for _, w, *_ in POLICY)
    for group in ("lockstep", "pin", "independent", "tag"):
        rows = [r for r in POLICY if r[0] == group]
        title = {
            "lockstep": "Lockstep (must equal the release version)",
            "pin": "Dependency pins (must accept the release version)",
            "independent": "Independently versioned (reported" + ("; --strict: must match)" if args.strict
                                                                   else "; not enforced)"),
            "tag": "Versioned by git tag (information)",
        }[group]
        print(title)
        for _, what, path, reader, kind in rows:
            if reader is None:
                status = "info" if (ROOT / path).is_file() else "MISSING"
                note = "no version field" if status == "info" else f"{path} not found"
                print(f"  {status:8} {what:{width}}  {note}")
                if status == "MISSING":
                    failures.append(f"{what}: {path} not found")
                continue
            try:
                got = reader()
            except LookupError as e:
                print(f"  {'ERROR':8} {what:{width}}  {e}")
                failures.append(f"{what}: {e}")
                continue
            if group == "pin":
                try:
                    ok = accepts(kind, got, want)
                except ValueError as e:
                    ok, got = False, f"{got} ({e})"
                status = "ok" if ok else "MISMATCH"
                print(f"  {status:8} {what:{width}}  {got!r}  ({path})")
                if not ok:
                    failures.append(f"{what}: {got!r} does not accept {want} ({path})")
                continue
            ok = same(kind, got, want)
            if group == "independent" and not args.strict:
                status = "ok" if ok else "own"
            else:
                status = "ok" if ok else "MISMATCH"
            print(f"  {status:8} {what:{width}}  {got}  ({path})")
            if not ok and (group == "lockstep" or args.strict):
                failures.append(f"{what}: declares {got}, release is {want} ({path})")
        print()

    baseline = f"sdk/libzxv/abi/libzxv-{core(want)}.json"
    print("ABI baseline (sdk/docs/STABILITY.md)")
    if args.no_baseline:
        print(f"  skipped  {baseline} (--no-baseline)")
    elif (ROOT / baseline).is_file():
        print(f"  ok       {baseline}")
    else:
        print(f"  MISSING  {baseline}: record it with")
        print(f"           python3 sdk/libzxv/tools/abi_check.py snapshot "
              f"sdk/libzxv/include/zxv_api.h {baseline}")
        failures.append(f"ABI baseline {baseline} is missing")
    print()

    if failures:
        print(f"FAILED: {len(failures)} problem(s)")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("All version checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
