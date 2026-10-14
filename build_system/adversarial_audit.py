#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Adversarial self-audit, static part (docs/ADVERSARIAL_AUDIT.md, stage S).

Asks the questions an attacker or a sceptical reviewer would ask of the
tree, the ones the normal gate cannot answer about itself: is a secret or a
binary checked in, does every test file actually run somewhere, which
modules nobody tests, which byte parsers no fuzzer reaches, can a pull
request steer a workflow, are dangerous C calls creeping in, and do the docs
point at files that exist.

Run from the repository root:

  build_system/adversarial_audit.py              print the report, exit 1 on FAIL
  build_system/adversarial_audit.py --json F     also write the findings as JSON
  build_system/adversarial_audit.py --list-allow show the accepted findings

A finding is FAIL (blocks), WARN (reported, does not block) or INFO.
Findings that are known and accepted are listed in
build_system/adversarial_audit.allow, one "<check> <key>  # reason" per
line; an allowed FAIL is reported as WARN. A line that no longer matches
anything is itself reported, so the list can only shrink.
"""
import json
import os
import re
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOW = os.path.join(ROOT, "build_system", "adversarial_audit.allow")

# Vendored third-party code is audited upstream; it is skipped by the
# source-level checks (not by the secret or binary checks).
VENDORED = re.compile(
    r"^kernel/src/pqsec/(mldsa|slhdsa|mlkem1024|hqc5)/|/third_party/|"
    r"^kernel/src/pay/xsd/|/vendor/|node_modules/")

# Directories whose code decides who may do what or holds keys: a stub that
# returns success there fails open.
SECURITY_DIRS = re.compile(
    r"^kernel/src/(pqsec|update|ident|identity|aipi|hwemu|bootlegger|tls|vinea|"
    r"vena|zab|mesh_net|recovery|estate|svault|ledger|pay|vino)/|"
    r"^kernel/arch/hosted/zxv(d_rpc|_http_guard|_update_host|_peer_host|_net_host)")


def git_files():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True,
                         capture_output=True).stdout
    return [f for f in out.decode().split("\0") if f]


def read(path, limit=None):
    try:
        with open(os.path.join(ROOT, path), "rb") as fh:
            data = fh.read(limit) if limit else fh.read()
    except OSError:
        return b""
    return data


def text(path):
    return read(path).decode("utf-8", "replace")


class Report:
    def __init__(self, allow):
        self.findings = []
        self.allow = allow
        self.used = set()

    def add(self, check, level, key, msg):
        if (check, key) in self.allow:
            self.used.add((check, key))
            if level == "FAIL":
                level = "WARN"
            msg += "  [accepted: %s]" % self.allow[(check, key)]
        self.findings.append({"check": check, "level": level, "key": key, "msg": msg})


def load_allow():
    allow = {}
    if not os.path.exists(ALLOW):
        return allow
    for line in open(ALLOW, encoding="utf-8"):
        body, _, reason = line.partition("#")
        parts = body.split()
        if len(parts) >= 2:
            allow[(parts[0], parts[1])] = reason.strip() or "no reason given"
    return allow


# ---- S1 secrets -------------------------------------------------------------
SECRET_PATTERNS = [
    ("private-key", re.compile(rb"-----BEGIN (?:[A-Z0-9]+ )*PRIVATE KEY-----")),
    ("github-token", re.compile(rb"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36}\b")),
    ("github-pat", re.compile(rb"\bgithub_pat_[A-Za-z0-9_]{60,}\b")),
    ("aws-key", re.compile(rb"\bAKIA[0-9A-Z]{16}\b")),
    ("hf-token", re.compile(rb"\bhf_[A-Za-z]{34}\b")),
    ("slack-token", re.compile(rb"\bxox[abprs]-[A-Za-z0-9-]{10,}\b")),
    ("openai-key", re.compile(rb"\bsk-[A-Za-z0-9]{40,}\b")),
    ("anthropic-key", re.compile(rb"\bsk-ant-[A-Za-z0-9_-]{30,}\b")),
]


def check_secrets(files, rep):
    for f in files:
        data = read(f, 4 << 20)
        for name, pat in SECRET_PATTERNS:
            m = pat.search(data)
            if m:
                line = data.count(b"\n", 0, m.start()) + 1
                rep.add("secrets", "FAIL", "%s:%s" % (f, name),
                        "%s:%d looks like a %s; remove it and rotate it" % (f, line, name))
        if re.search(r"(^|/)(id_rsa|id_ed25519|.*_priv\.pem|.*\.p12|.*\.pfx|\.env)$", f):
            rep.add("secrets", "FAIL", f, "%s is a key or env file by name" % f)


# ---- S2 checked-in executables ----------------------------------------------
MAGIC = {b"\x7fELF": "ELF", b"MZ": "PE", b"\xcf\xfa\xed\xfe": "Mach-O",
         b"\xca\xfe\xba\xbe": "Mach-O fat"}


def check_binaries(files, rep):
    for f in files:
        if f.startswith("fuzz/corpus/") or "/corpus/" in f or "/testdata/" in f:
            continue  # fuzz inputs are meant to look like anything
        head = read(f, 4)
        for mg, kind in MAGIC.items():
            if head.startswith(mg) and not (kind == "PE" and f.endswith((".txt", ".md"))):
                rep.add("binaries", "FAIL", f,
                        "%s is a %s executable; build it, do not check it in" % (f, kind))
                break


# ---- S3 test files that no build runs ---------------------------------------
BUILD_FILES = re.compile(
    r"(^|/)(Makefile[^/]*|[^/]*\.mk|[^/]*\.sh|CMakeLists\.txt|[^/]*\.cmake|"
    r"build\.gradle(\.kts)?|Package\.swift|[^/]*\.csproj|Cargo\.toml)$|"
    r"^\.github/workflows/")


def check_orphan_tests(files, rep):
    corpus = "\n".join(text(f) for f in files if BUILD_FILES.search(f))
    for f in files:
        if not f.endswith(".c") or VENDORED.search(f):
            continue
        base = os.path.basename(f)[:-2]
        if not base.startswith("test_"):
            continue
        if not re.search(r"\b%s\b" % re.escape(base), corpus):
            rep.add("orphan-tests", "FAIL", f,
                    "%s is a test that no Makefile, script or workflow builds, so "
                    "whatever it checks is not checked" % f)


# ---- S3b tests that test their own copy of the code ------------------------
FUNC_DEF = re.compile(r"^(?!static\b)(?:[A-Za-z_][\w]*[\s\*]+)+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{",
                      re.M)


def check_shadow_tests(files, rep):
    defs = defaultdict(set)
    tests = []
    for f in files:
        if not f.endswith(".c") or VENDORED.search(f):
            continue
        if not (f.startswith("kernel/") or f.startswith("tests/") or f.startswith("sdk/")):
            continue
        if os.path.basename(f).startswith("test_"):
            tests.append(f)
            continue
        for name in FUNC_DEF.findall(text(f)):
            defs[name].add(f)
    for t in tests:
        body = text(t)
        mine = set(FUNC_DEF.findall(body)) - {"main"}
        # A test may stub a hardware hook; it should not re-implement logic.
        # Only count functions with a real body (more than a few lines).
        hits = []
        for name in sorted(mine & set(defs)):
            m = re.search(r"\b%s\s*\([^;{}]*\)\s*\{" % re.escape(name), body)
            depth, i, lines = 0, m.end() - 1, 0
            while i < len(body):
                c = body[i]
                depth += c == "{"
                depth -= c == "}"
                lines += c == "\n"
                if depth == 0:
                    break
                i += 1
            if lines >= 8:
                hits.append(name)
        if hits:
            rep.add("shadow-tests", "WARN", t,
                    "%s defines its own %s, which the tree also defines in %s: the test "
                    "may be checking its copy instead of the shipped code"
                    % (t, ", ".join(hits[:5]), ", ".join(sorted(defs[hits[0]])[:2])))


# ---- S4 modules with no test at all -----------------------------------------
def check_untested_modules(files, rep):
    mods = defaultdict(lambda: {"src": 0, "test": 0})
    for f in files:
        m = re.match(r"kernel/src/([^/]+)/.*\.c$", f)
        if not m or VENDORED.search(f):
            continue
        key = "src" if not os.path.basename(f).startswith("test_") else "test"
        mods[m.group(1)][key] += 1
    fuzzmk = text("fuzz/Makefile")
    host = "\n".join(text(f) for f in files if f.startswith("tests/") and f.endswith(".c"))
    # A recipe line that builds some test_* and names src/<mod>/ links the module.
    recipes = "\n".join(l for f in files if BUILD_FILES.search(f)
                        for l in text(f).replace("\\\n", " ").splitlines() if "test_" in l)
    for mod, n in sorted(mods.items()):
        if n["test"] or n["src"] == 0:
            continue
        if re.search(r"\$\(S\)/%s/" % re.escape(mod), fuzzmk):
            continue
        if re.search(r'#include\s+"[^"]*\b%s/' % re.escape(mod), host):
            continue
        if re.search(r"(^|[\s/])src/%s/" % re.escape(mod), recipes):
            continue
        rep.add("untested-modules", "WARN", mod,
                "kernel/src/%s has %d C files and no test_*.c, host test or fuzz harness"
                % (mod, n["src"]))


# ---- S5 byte parsers no fuzzer reaches --------------------------------------
PARSER_DECL = re.compile(
    r"^[A-Za-z_][\w \*]*?\b(\w*(?:parse|decode|unpack|unwrap|deserial|load|open|verify)\w*)"
    r"\s*\([^;{]*(?:const\s+(?:uint8_t|unsigned char|char|void)\s*\*)[^;{]*\)\s*;",
    re.M)


def check_unfuzzed_parsers(files, rep):
    fuzzmk = text("fuzz/Makefile")
    fuzzed = set(re.findall(r"\$\(S\)/([\w]+)/", fuzzmk))
    harness_text = "\n".join(text(f) for f in files
                             if f.startswith("fuzz/") and f.endswith(".c"))
    for f in files:
        m = re.match(r"kernel/src/([^/]+)/[^/]+\.h$", f)
        if not m or VENDORED.search(f) or m.group(1) in fuzzed:
            continue
        names = sorted(set(PARSER_DECL.findall(text(f))))
        names = [n for n in names if n not in harness_text]
        if names:
            shown = ", ".join(names[:6]) + (" ..." if len(names) > 6 else "")
            rep.add("unfuzzed-parsers", "WARN", f,
                    "%s declares %d byte-input entry point(s) with no fuzz harness: %s"
                    % (f, len(names), shown))


# ---- S6 workflow hardening ---------------------------------------------------
UNTRUSTED_EXPR = re.compile(
    r"\$\{\{\s*github\.(?:event\.(?:issue|pull_request|comment|review|review_comment|"
    r"discussion|pages|head_commit|commits)\.[\w.\[\]*]*(?:title|body|name|message|"
    r"label|ref|email|page_name)|head_ref)\s*\}\}")


def check_workflows(files, rep):
    for f in files:
        if not re.match(r"\.github/workflows/.*\.ya?ml$", f):
            continue
        body = text(f)
        if re.search(r"^\s*pull_request_target\s*:", body, re.M) or \
                re.search(r"^on:.*pull_request_target", body, re.M):
            rep.add("workflows", "FAIL", f + ":pull_request_target",
                    "%s runs on pull_request_target, which gives fork code the "
                    "base repo's token and secrets" % f)
        if not re.search(r"^permissions\s*:", body, re.M):
            rep.add("workflows", "FAIL", f + ":permissions",
                    "%s has no top-level permissions block, so jobs get the "
                    "repository's default token scope" % f)
        if re.search(r"permissions\s*:\s*write-all", body):
            rep.add("workflows", "FAIL", f + ":write-all", "%s grants write-all" % f)
        # Untrusted text expanded inside a shell script is script injection.
        in_run = False
        run_indent = 0
        for i, line in enumerate(body.splitlines(), 1):
            s = line.lstrip()
            ind = len(line) - len(s)
            if re.match(r"(-\s+)?run\s*:", s):
                in_run, run_indent = True, ind
                if UNTRUSTED_EXPR.search(s):
                    rep.add("workflows", "FAIL", "%s:%d" % (f, i),
                            "%s:%d expands attacker-controlled text inside run:; pass it "
                            "through env: instead" % (f, i))
                continue
            if in_run and s and ind <= run_indent:
                in_run = False
            if in_run and UNTRUSTED_EXPR.search(line):
                rep.add("workflows", "FAIL", "%s:%d" % (f, i),
                        "%s:%d expands attacker-controlled text inside run:; pass it "
                        "through env: instead" % (f, i))
            if re.search(r"curl[^|\n]*\|\s*(sudo\s+)?(ba|z)?sh\b", line):
                rep.add("workflows", "WARN", "%s:%d:curl-sh" % (f, i),
                        "%s:%d pipes a download straight into a shell" % (f, i))
    # Third-party actions on a moving tag (first-party actions/* and github/* excepted).
    tags = defaultdict(set)
    for f in files:
        if re.match(r"\.github/workflows/.*\.ya?ml$", f):
            for m in re.finditer(r"uses:\s*([\w.-]+/[\w./-]+)@([\w.-]+)", text(f)):
                act, ref = m.group(1), m.group(2)
                if act.split("/")[0] in ("actions", "github"):
                    continue
                if not re.fullmatch(r"[0-9a-f]{40}", ref):
                    tags[act].add(ref)
    for act, refs in sorted(tags.items()):
        rep.add("workflows", "WARN", "unpinned:" + act,
                "third-party action %s is used at a moving tag (%s), not a commit SHA"
                % (act, ", ".join(sorted(refs))))


# ---- S7 fail-open stubs in security code ------------------------------------
STUB = re.compile(r"\b(TODO|FIXME|XXX|stub|not implemented|unimplemented|placeholder)\b", re.I)
OK_RET = re.compile(r"return\s+(0|true|1|ZXV_OK|\w+_OK)\s*;")


def check_fail_open(files, rep):
    for f in files:
        if not f.endswith(".c") or VENDORED.search(f) or not SECURITY_DIRS.search(f):
            continue
        if os.path.basename(f).startswith("test_"):
            continue
        lines = text(f).splitlines()
        for i, line in enumerate(lines):
            if STUB.search(line):
                window = lines[i:i + 4]
                if any(OK_RET.search(w) for w in window):
                    rep.add("fail-open", "WARN", "%s:%d" % (f, i + 1),
                            "%s:%d stub/TODO next to a success return in security code: "
                            "%s" % (f, i + 1, line.strip()[:100]))


# ---- S8 dangerous C calls ----------------------------------------------------
DANGER = [
    ("gets", re.compile(r"(?<![\w.>])gets\s*\("), "FAIL"),
    ("strcpy", re.compile(r"(?<![\w.>])strcpy\s*\("), "WARN"),
    ("strcat", re.compile(r"(?<![\w.>])strcat\s*\("), "WARN"),
    ("sprintf", re.compile(r"(?<![\w.>])v?sprintf\s*\("), "WARN"),
    ("system", re.compile(r"(?<![\w.>])(system|popen)\s*\("), "WARN"),
]
CRYPTO_DIRS = re.compile(r"^kernel/src/(pqsec|mlkem|tls|bootlegger|ident|identity|aipi|"
                         r"update|vinea|zab|robin_debanks|hwemu)/")


def check_dangerous_calls(files, rep):
    counts = defaultdict(list)
    for f in files:
        if not f.endswith(".c") or VENDORED.search(f) or os.path.basename(f).startswith("test_"):
            continue
        if not (f.startswith("kernel/") or f.startswith("sdk/libzxv/")):
            continue
        src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"),
                     text(f), flags=re.S)
        for i, line in enumerate(src.splitlines(), 1):
            code = line.split("//")[0]
            for name, pat, level in DANGER:
                if pat.search(code):
                    counts[(name, level)].append("%s:%d" % (f, i))
            if CRYPTO_DIRS.search(f) and re.search(r"(?<![\w.>])s?rand\s*\(", code):
                counts[("rand-in-crypto", "FAIL")].append("%s:%d" % (f, i))
    for (name, level), where in sorted(counts.items()):
        if level == "FAIL":
            for w in where:
                rep.add("dangerous-calls", "FAIL", "%s:%s" % (name, w),
                        "%s calls %s()" % (w, name))
        else:
            rep.add("dangerous-calls", "WARN", name,
                    "%d call(s) to %s() in kernel code, e.g. %s"
                    % (len(where), name, ", ".join(where[:3])))


# ---- S9 docs that point at files that do not exist --------------------------
LINK = re.compile(r"\]\(([^)#\s]+)(?:#[^)]*)?\)")


def check_doc_links(files, rep):
    tracked = set(files)
    dirs = {os.path.dirname(f) for f in files}
    alld = set()
    for d in dirs:
        while d:
            alld.add(d)
            d = os.path.dirname(d)
    for f in files:
        if not f.endswith(".md") or VENDORED.search(f):
            continue
        for m in LINK.finditer(text(f)):
            target = m.group(1)
            if re.match(r"[a-z]+:|//|mailto:|<", target) or target.startswith("/"):
                continue
            path = os.path.normpath(os.path.join(os.path.dirname(f), target))
            if path.startswith(".."):
                continue
            if path not in tracked and path not in alld:
                rep.add("doc-links", "WARN", "%s->%s" % (f, target),
                        "%s links to %s, which is not in the tree" % (f, target))


# ---- S10 fixed-point constants that do not survive conversion ---------------
# SR_FROM_FLOAT folds a literal into Q32.32 (surplus.h): a step of 2^-32 and an
# integer range of +-2^31. A physical constant in SI units (hbar = 1.05e-34)
# silently becomes 0 there.
SR_LIT = re.compile(r"SR_FROM_FLOAT\s*\(\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)\s*\)")


def check_fixed_constants(files, rep):
    for f in files:
        if not f.endswith((".c", ".h")) or VENDORED.search(f) or not f.startswith("kernel/"):
            continue
        for i, line in enumerate(text(f).splitlines(), 1):
            for m in SR_LIT.finditer(line):
                v = float(m.group(1))
                q = int(v * 2 ** 32)
                if v == 0:
                    continue
                if abs(v) >= 2 ** 31:
                    why = "overflows Q32.32 (|x| >= 2^31)"
                elif q == 0:
                    why = "rounds to 0 in Q32.32"
                elif abs(q / 2 ** 32 - v) / abs(v) > 0.01:
                    why = "loses %.0f%% of its value in Q32.32" % (
                        100 * abs(q / 2 ** 32 - v) / abs(v))
                else:
                    continue
                rep.add("fixed-constants", "FAIL", "%s:%d" % (f, i),
                        "%s:%d %s %s; scale the units" % (f, i, m.group(0), why))


CHECKS = [
    ("secrets", check_secrets),
    ("binaries", check_binaries),
    ("orphan-tests", check_orphan_tests),
    ("shadow-tests", check_shadow_tests),
    ("untested-modules", check_untested_modules),
    ("unfuzzed-parsers", check_unfuzzed_parsers),
    ("workflows", check_workflows),
    ("fail-open", check_fail_open),
    ("dangerous-calls", check_dangerous_calls),
    ("doc-links", check_doc_links),
    ("fixed-constants", check_fixed_constants),
]


def main(argv):
    allow = load_allow()
    if "--list-allow" in argv:
        for (c, k), r in sorted(allow.items()):
            print("%-18s %s  # %s" % (c, k, r))
        return 0
    files = git_files()
    rep = Report(allow)
    for _, fn in CHECKS:
        fn(files, rep)
    for key in sorted(set(allow) - rep.used):
        rep.add("allow-list", "WARN", "%s %s" % key,
                "allow-list entry '%s %s' matches nothing any more; delete it" % key)

    order = {"FAIL": 0, "WARN": 1, "INFO": 2}
    by_check = defaultdict(list)
    for fd in rep.findings:
        by_check[fd["check"]].append(fd)
    print("=== Adversarial self-audit, static stage (docs/ADVERSARIAL_AUDIT.md) ===")
    for name, _ in CHECKS + [("allow-list", None)]:
        fs = sorted(by_check.get(name, []), key=lambda d: (order[d["level"]], d["key"]))
        nfail = sum(d["level"] == "FAIL" for d in fs)
        nwarn = sum(d["level"] == "WARN" for d in fs)
        if name == "allow-list" and not fs:
            continue
        verdict = "FAIL" if nfail else ("WARN" if nwarn else "PASS")
        print("[%s] %-17s %d fail, %d warn" % (verdict, name, nfail, nwarn))
        for d in fs[:40]:
            print("    %s %s" % (d["level"], d["msg"]))
        if len(fs) > 40:
            print("    ... %d more (use --json for all)" % (len(fs) - 40))
    if "--json" in argv:
        out = argv[argv.index("--json") + 1]
        with open(out, "w", encoding="utf-8") as fh:
            json.dump(rep.findings, fh, indent=1)
    fails = sum(d["level"] == "FAIL" for d in rep.findings)
    print("=== %d FAIL, %d WARN ===" % (
        fails, sum(d["level"] == "WARN" for d in rep.findings)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
