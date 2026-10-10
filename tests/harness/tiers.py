#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""tiers.py: build and run the tiered tests (tests/01_root_axioms,
tests/02_integration, tests/03_metamorphic) and the existing module tests the
mutation harness uses as its "before" baseline.

    python3 tests/harness/tiers.py [--mode plain|san|q32|fast|all] SUITE...
    python3 tests/harness/tiers.py --list

Modes
  plain  -O0, -DTEST_HOST (double surplus_real_t)
  san    plain + AddressSanitizer + LeakSanitizer + UBSan, -fno-sanitize-recover:
         proves the suites have no memory errors, leaks or UB of their own
         (known UB in the code under test is recorded, see tier.h)
  q32    no -DTEST_HOST: surplus_real_t is Q32.32 (only for suites that
         depend on it; others skip)
  fast   -O0, no sanitizer: what the mutation harness uses
  all    plain, san and q32 in turn (the default)

Every program is built into a fresh private directory (nothing under a fixed
/tmp path), so several worktrees can run this at the same time. Set
ZXV_OBJ_CACHE=<dir> to reuse object files for sources other than
$ZXV_MUT_TARGET (the mutation harness does; keys include the file content).

Exit status: 0 when every program built and passed, 3 when the only failures
were builds, 1 otherwise.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KERNEL = os.path.join(ROOT, "kernel")
T1 = "../tests/01_root_axioms"
T2 = "../tests/02_integration"
T3 = "../tests/03_metamorphic"

BASE = ["-std=c11", "-Wall", "-Werror", "-Wextra", "-Iinclude", "-Isrc/modbind"]
MODES = {
    "plain": ["-O0", "-g", "-DTEST_HOST"],
    "san": ["-O1", "-g", "-DTEST_HOST", "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all"],
    "q32": ["-O0", "-g"],
    "fast": ["-O0", "-DTEST_HOST"],
}

# ---- source groups (paths relative to kernel/) ----
INC_T1 = ["-I" + T1]
RAT = dict(inc=["src/rational", "src/rmag", "src/surplus"],
           src=["src/rational/rational.c", "src/rmag/rmag_core.c"])
PAY_INC = ["src/pay", "src/tensor", "src/mlkem", "src/swarm", "src/robin_debanks", "src/zcapital",
           "src/surplus"]
PAY_SRC = ["src/pay/pay_crypto.c", "src/pay/pay_equity.c", "src/pay/pay_farm.c",
           "src/pay/pay_iso.c", "src/pay/pay_iso_parse.c", "src/pay/pay_ledger.c",
           "src/pay/pay_roles.c", "src/pay/pay_tables.c", "src/pay/pay_tithe.c",
           "src/pay/pay_treasury.c", "src/pay/pay_util.c", "src/tensor/zt.c",
           "src/mlkem/keccak.c", "src/robin_debanks/sha256.c", "src/swarm/swarm_market.c",
           "src/swarm/swarm_budget.c", "src/swarm/swarm_emotion.c"]
TL_INC = ["src/finance", "src/vino", "src/surplus", "src/edp_risk", "src/identity",
          "src/predictive"]
TL_SRC = ["src/finance/triple_ledger.c", "src/surplus/surplus.c", "src/edp_risk/edp_risk.c"]
CC_INC = ["src", "src/event_space", "src/robin_debanks", "src/surplus", "src/rational",
          "src/crit168", "src/count_house", "src/porter_house", "src/edp_risk", "src/telemetry",
          "src/oseq", "src/rmag", "src/lpres", "src/vino", "src/identity", "src/finance",
          "src/license", "src/net", "src/predictive", "src/situation", "src/axiom_matrix",
          "src/iphase", "src/choice", "src/phase_coord", "src/dual_space", "src/lattice",
          "src/community_chest", "src/sched", "src/mm"]
CC_SRC = ["src/community_chest/community_chest.c", "src/vino/vino.c",
          "src/count_house/count_house.c", "src/robin_debanks/sha256.c",
          "src/robin_debanks/ed25519_verify.c", "src/robin_debanks/crypto_verify.c",
          "src/edp_risk/edp_risk.c", "src/surplus/surplus.c", "src/porter_house/porter_house.c",
          "src/lpres/lpres.c", "src/lpres/lpres_core.c", "src/rmag/rmag_core.c", "src/rmag/rmag.c",
          "src/oseq/oseq_core.c", "src/event_space/event_envelope.c"]
ALLOC_INC = ["src/mm", "src/rmag", "src/audio", "src/surplus", "src/edp_risk"]
ALLOC_SRC = ["src/mm/mm.c", "src/rmag/rmag_core.c", "src/audio/audio.c", "src/edp_risk/edp_risk.c",
             "src/surplus/surplus.c"]


def prog(name, main, inc, src, args=(), modes=("plain", "san", "q32", "fast"), extra=(),
         pre=None, base=True):
    return dict(name=name, main=main if isinstance(main, list) else [main], inc=list(inc),
                src=list(src), args=list(args), modes=set(modes), extra=list(extra), pre=pre,
                base=base)


NOQ32 = ("plain", "san", "fast")  # suites whose code needs TEST_HOST (double) or is mode-free

SUITES = {
    # ---- Tier 1: root axioms ----
    "t1_rational": [prog("axioms_rational", T1 + "/axioms_rational.c", RAT["inc"] + [T1],
                         RAT["src"], modes=NOQ32)],
    "t1_pay": [prog("axioms_pay", T1 + "/axioms_pay.c", PAY_INC + [T1], PAY_SRC, modes=NOQ32)],
    "t1_triple": [prog("axioms_triple", T1 + "/axioms_triple.c", TL_INC + [T1], TL_SRC)],
    "t1_chest": [prog("axioms_chest", T1 + "/axioms_chest.c", CC_INC + [T1], CC_SRC,
                      modes=NOQ32)],
    "t1_alloc": [prog("axioms_alloc", [T1 + "/axioms_alloc.c", T1 + "/fs_heap_peer.c"],
                      ALLOC_INC + [T1], ALLOC_SRC, modes=NOQ32)],
    # ---- Tier 2: integration ----
    "t2_escrow": [prog("integ_escrow", T2 + "/integ_escrow.c", CC_INC + [T1],
                       CC_SRC + ["src/finance/triple_ledger.c"])],
    "t2_pay": [prog("integ_pay", T2 + "/integ_pay.c", PAY_INC + [T1], PAY_SRC, modes=NOQ32)],
    "t2_count_house": [prog("integ_count_house", T2 + "/integ_count_house.c", CC_INC + [T1],
                            CC_SRC)],
    # ---- Tier 3: metamorphic ----
    "t3_rational": [prog("meta_rational", T3 + "/meta_rational.c", RAT["inc"] + [T1], RAT["src"],
                         modes=NOQ32)],
    "t3_ledger": [prog("meta_ledger", T3 + "/meta_ledger.c", PAY_INC + [T1], PAY_SRC,
                       modes=NOQ32)],
    "t3_chest": [prog("meta_chest", T3 + "/meta_chest.c", CC_INC + [T1],
                      CC_SRC + ["src/finance/triple_ledger.c"], modes=NOQ32)],
    # ---- the existing module tests (mutation "before" baseline); same
    #      sources and flags as their kernel/Makefile verify-all recipes ----
    "legacy_rational": [
        prog("test_rational", "src/rational/test_rational.c", ["src/rational"],
             ["src/rational/rational.c"], modes=NOQ32, base=False),
        prog("test_rational_regress", "src/rational/test_rational_regress.c", ["src/rational"],
             ["src/rational/rational.c"], modes=NOQ32, base=False)],
    "legacy_rmag": [prog("test_rmag_core", "src/rmag/test_rmag.c",
                         ["src/rmag", "src/surplus", "src/oseq", "src/lpres"],
                         ["src/rmag/rmag_core.c"], modes=NOQ32, extra=["-D_GNU_SOURCE"])],
    "legacy_pay": [prog("test_pay", "src/pay/test_pay.c", PAY_INC, PAY_SRC, modes=NOQ32,
                        args=["{out}/tithe_ref.txt", "src/pay/xsd", "{out}/xml"],
                        pre=[["python3", "-I", "src/pay/gen_tithe_ref.py", "{out}/tithe_ref.txt"],
                             ["mkdir", "-p", "{out}/xml"]])],
    "legacy_vino": [prog("test_vino", "src/vino/test_vino.c",
                         ["src/vino", "src/rmag", "src/lpres", "src/surplus"],
                         ["src/vino/vino.c", "src/rmag/rmag_core.c", "src/lpres/lpres_core.c"],
                         modes=NOQ32)],
    "legacy_cc": [prog("test_community_chest", "src/community_chest/test_community_chest.c",
                       CC_INC, CC_SRC, modes=NOQ32)],
    "legacy_count_house": [
        prog("test_count_house", "src/count_house/test_count_house.c", CC_INC,
             ["src/count_house/count_house.c", "src/robin_debanks/sha256.c",
              "src/robin_debanks/ed25519_verify.c", "src/robin_debanks/crypto_verify.c",
              "src/edp_risk/edp_risk.c", "src/surplus/surplus.c",
              "src/porter_house/porter_house.c", "src/lpres/lpres.c"], modes=NOQ32),
        prog("test_count_house_fractal", "src/count_house/test_count_house_fractal.c", CC_INC,
             ["src/count_house/count_house.c", "src/robin_debanks/sha256.c",
              "src/robin_debanks/ed25519_verify.c", "src/robin_debanks/crypto_verify.c",
              "src/edp_risk/edp_risk.c", "src/surplus/surplus.c",
              "src/porter_house/porter_house.c", "src/lpres/lpres.c"], modes=NOQ32)],
    "legacy_finance": [prog("test_finance_core", "src/finance/test_finance_core.c", TL_INC,
                            ["src/finance/triple_ledger.c", "src/finance/rails.c",
                             "src/finance/crypto_bridge.c", "src/surplus/surplus.c",
                             "src/edp_risk/edp_risk.c"])],
    "legacy_porter": [prog("test_porter_house", "src/porter_house/test_porter_house.c",
                           ["src/event_space", "src/porter_house", "src/surplus", "src/edp_risk"],
                           ["src/porter_house/porter_house.c",
                            "src/event_space/event_envelope.c"], modes=NOQ32)],
    "legacy_audio": [prog("test_audio", "src/audio/test_audio.c",
                          ["src/audio", "src/edp_risk", "src/surplus", "src/crit168"],
                          ["src/audio/audio.c", "src/edp_risk/edp_risk.c",
                           "src/surplus/surplus.c"], modes=NOQ32)],
}
GROUPS = {
    "tier1": ["t1_rational", "t1_pay", "t1_triple", "t1_chest", "t1_alloc"],
    "tier2": ["t2_escrow", "t2_pay", "t2_count_house"],
    "tier3": ["t3_rational", "t3_ledger", "t3_chest"],
}
GROUPS["tiers"] = GROUPS["tier1"] + GROUPS["tier2"] + GROUPS["tier3"]


def sh(cmd, cwd, env=None, quiet=False, timeout=None):
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=timeout)
    out = p.stdout.decode("utf-8", "replace")
    return p.returncode, out


def cc():
    return os.environ.get("ZXV_TIER_CC", "gcc")  # not $CC: the kernel build sets a cross CC


def compile_obj(src, flags, out_dir, cache):
    """Compile one source to an object, through the cache when allowed."""
    target = os.environ.get("ZXV_MUT_TARGET", "")
    abs_src = os.path.normpath(os.path.join(KERNEL, src))
    obj = os.path.join(out_dir, hashlib.sha1(src.encode()).hexdigest()[:12] + ".o")
    use_cache = cache and target and not target.endswith(".h") and \
        os.path.normpath(os.path.join(ROOT, target)) != abs_src
    key = None
    if use_cache:
        with open(abs_src, "rb") as f:
            h = hashlib.sha256(f.read())
        h.update(" ".join(flags).encode())
        h.update(src.encode())
        key = os.path.join(cache, h.hexdigest() + ".o")
        if os.path.exists(key):
            return 0, key, ""
    rc, out = sh([cc()] + flags + ["-c", src, "-o", obj], KERNEL)
    if rc == 0 and key:
        tmp = key + ".%d.tmp" % os.getpid()
        shutil.copyfile(obj, tmp)
        os.replace(tmp, key)
    return rc, obj, out


def build_and_run(p, mode, out_root, verbose, timeout):
    flags = (BASE if p["base"] else ["-std=c11", "-Wall", "-Werror", "-Wextra", "-Iinclude"]) + \
        MODES[mode] + p["extra"] + ["-I" + i for i in p["inc"]]
    out = os.path.join(out_root, p["name"] + "." + mode)
    os.makedirs(out, exist_ok=True)
    cache = os.environ.get("ZXV_OBJ_CACHE")
    if cache:
        os.makedirs(cache, exist_ok=True)
    objs = []
    log = []
    for s in p["main"] + p["src"]:
        rc, obj, o = compile_obj(s, flags, out, cache)
        log.append(o)
        if rc != 0:
            return "build", "".join(log)
        objs.append(obj)
    exe = os.path.join(out, p["name"])
    link_flags = [f for f in MODES[mode] if f.startswith("-fsanitize") or f == "-g"]
    rc, o = sh([cc()] + link_flags + objs + ["-lm", "-o", exe], KERNEL)
    if rc != 0:
        return "build", "".join(log) + o
    for pre in p["pre"] or []:
        rc, o = sh([a.replace("{out}", out) for a in pre], KERNEL)
        if rc != 0:
            return "fail", o
    env = dict(os.environ)
    env.setdefault("ASAN_OPTIONS", "detect_leaks=1:abort_on_error=0")
    env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    args = [a.replace("{out}", out) for a in p["args"]]
    try:
        rc, o = sh([exe] + args, KERNEL, env=env, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "timeout", ""
    return ("pass" if rc == 0 else "fail"), o


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("suites", nargs="*")
    ap.add_argument("--mode", default="all", choices=["all"] + list(MODES))
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep", action="store_true", help="keep the build directory")
    ap.add_argument("-v", "--verbose", action="store_true", help="print each program's output")
    ap.add_argument("-q", "--quiet", action="store_true", help="print only failures")
    ap.add_argument("--timeout", type=float, default=600.0)
    ap.add_argument("--fail-fast", action="store_true", help="stop at the first failure")
    a = ap.parse_args(argv)
    if a.list or not a.suites:
        for k in GROUPS:
            print("%-16s %s" % (k, " ".join(GROUPS[k])))
        for k, v in SUITES.items():
            print("%-16s %s" % (k, " ".join(p["name"] for p in v)))
        return 0 if a.list else 2
    names = []
    for s in a.suites:
        names += GROUPS.get(s, [s])
    for n in names:
        if n not in SUITES:
            print("unknown suite %s (try --list)" % n)
            return 2
    modes = ["plain", "san", "q32"] if a.mode == "all" else [a.mode]
    out_root = tempfile.mkdtemp(prefix="zxv_tiers_")
    bad = 0
    build_only = True
    try:
        for n in names:
            for p in SUITES[n]:
                for m in modes:
                    if m not in p["modes"]:
                        continue
                    if a.fail_fast and bad:
                        break
                    st, o = build_and_run(p, m, out_root, a.verbose, a.timeout)
                    summary = ""
                    for line in o.splitlines():
                        if line.startswith("[PASS] tier") or line.startswith("[FAIL] tier"):
                            summary = line.split(":", 1)[-1].strip()
                    if st != "pass":
                        bad += 1
                        build_only = build_only and st == "build"
                        print("[FAIL] %s/%s (%s) %s" % (n, p["name"], m, st))
                        print("\n".join(o.splitlines()[-40:]))
                    elif not a.quiet:
                        print("[PASS] %s/%s (%s) %s" % (n, p["name"], m, summary))
                    if a.verbose and st == "pass":
                        print(o)
    finally:
        if not a.keep:
            shutil.rmtree(out_root, ignore_errors=True)
        else:
            print("build directory kept: " + out_root)
    if bad and build_only:
        return 3  # only builds failed (the mutation harness counts this as compile-error)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
