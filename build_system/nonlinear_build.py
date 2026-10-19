#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""nonlinear_build.py — drive the kernel build by the lattice schedule the
GPU computed. The strata map is already projected onto the Fibonacci shells
(gen_lattice_routing.assign); this driver expands each stage's modules into
the .c files they cover, and compiles every file of a stage in parallel —
the waves are ring-ordered, every dependency of a stage lives in a strictly
lower ring, so intra-stage parallelism is race-free by construction.

Usage: nonlinear_build.py <strata.txt> <src root> <out dir> [<cc> <cflags...>]
       nonlinear_build.py <strata.txt> <src root> <out dir> --srcs <file> --platform <file> [<cc> <cflags...>]

For each stage k it writes <out>/stage_<k>.list and compiles the listed
sources to <out>/stage_<k>/*.o with -j parallelism (default nproc).
--srcs restricts compilation to the paths in <file> (one per line), so the
waves cover exactly the objects the real kernel links, not host-tier test or
bench programs that also live under the source root. --platform lists the
sources outside the strata root (arch glue, the freestanding shim): they
form stage -1, the platform wave the manifold stands on.
"""
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_lattice_routing import assign, parse_strata  # noqa: E402


def module_sources(root, name, pinned):
    """a strata path covers either one file (foo or foo.c) or a directory.
    When a directory expands, files pinned to their own strata line are
    excluded — they compile in their own pin's wave, not the directory's."""
    p = root / name
    if p.is_dir():
        out = []
        for x in sorted(p.rglob("*.c")):
            rel = str(x.relative_to(root))
            if rel in pinned or rel[:-2] in pinned:
                continue
            # an unpinned file under a pinned subdir belongs to the subdir's
            # wave, not this one — check every ancestor dir too
            anc = x.parent.relative_to(root)
            while str(anc) != "." and str(anc) != str(p.relative_to(root)):
                if str(anc) in pinned:
                    break
                anc = anc.parent
            else:
                out.append(x)
        return out
    for cand in (root / (name + ".c"), root / name):
        if cand.suffix == ".c" and cand.is_file():
            return [cand]
    return []


def main(argv):
    strata, root, out = pathlib.Path(argv[1]), pathlib.Path(argv[2]), \
        pathlib.Path(argv[3])
    args = argv[4:]
    allow = None
    platform_srcs = []
    while args[:1] in (["--srcs"], ["--platform"]):
        flag = args[0]
        lst = [pathlib.Path(l.strip())
               for l in pathlib.Path(args[1]).read_text().splitlines()
               if l.strip()]
        if flag == "--srcs":
            allow = {str(p.resolve()) for p in lst}
        else:
            # arch glue, freestanding shim, generated sources: outside the
            # strata root, so they form stage -1 — the platform wave the
            # manifold stands on, compiled before ring 1.
            platform_srcs = [p.resolve() for p in lst]
        args = args[2:]
    cc = args[0] if args else "gcc"
    cflags = args[1:] if len(args) > 1 else ["-std=c11", "-O2", "-c"]

    assign_ = assign(parse_strata(strata))
    stages = {}
    for path, (cell, ring) in assign_.items():
        stages.setdefault(ring, []).append(path)

    out.mkdir(parents=True, exist_ok=True)
    total, fails = 0, 0
    covered = set()
    waves = {}
    if platform_srcs:
        waves[-1] = platform_srcs
    for ring in sorted(stages):
        srcs = []
        for mod in sorted(stages[ring]):
            srcs += module_sources(root, mod, assign_)
        if allow is not None:
            srcs = [s for s in srcs if str(s.resolve()) in allow]
        waves[ring] = srcs
    for ring in sorted(waves):
        # ring k is one build wave: every source compiles in parallel, and
        # the wave completes before ring k+1 starts — the scaffold's
        # guarantee is that all of ring k's dependencies live below it.
        srcs = waves[ring]
        covered.update(str(s.resolve()) for s in srcs)
        (out / f"stage_{ring}.list").write_text(
            "\n".join(str(s) for s in srcs) + "\n")
        sdir = out / f"stage_{ring}"
        sdir.mkdir(exist_ok=True)
        t0 = __import__("time").time()
        # bounded parallelism: one worker lane per core, stage-wide.
        import os
        lanes = max(1, min(len(srcs), os.cpu_count() or 4))
        fails_ = 0
        running = []
        inflight = 0
        pending = list(srcs)
        while pending or running:
            while pending and inflight < lanes:
                src = pending.pop(0)
                # mirror the source path in the object name: two modules can
                # carry same-named files, and a flat name would silently
                # overwrite the earlier object.
                try:
                    oname = "__".join(src.relative_to(root).parts)
                except ValueError:
                    # platform wave: sources outside the strata root
                    oname = "__".join(src.resolve().parts[-4:])
                oname += ".o"
                running.append((src, subprocess.Popen(
                    [cc, *cflags, "-o", str(sdir / oname), str(src)],
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)))
                inflight += 1
            done = [(s, p) for s, p in running if p.poll() is not None]
            if not done:
                __import__("time").sleep(0.01)
                continue
            for src, p in done:
                running.remove((src, p))
                inflight -= 1
                if p.returncode != 0:
                    fails_ += 1
                    sys.stderr.write(f"[FAIL] {src}\n")
                    sys.stderr.write(p.stderr.read().decode()[:800])
        fails += fails_
        total += len(srcs)
        nmod = len(stages.get(ring, platform_srcs))
        print(f"[stage {ring}] {nmod} modules, {len(srcs)} "
              f"sources, {__import__('time').time() - t0:.2f}s"
              + (f" ({fails_} failed)" if fails_ else ""))
    print(f"non-linear build: {total} sources, {fails} failed")
    if allow is not None:
        missed = sorted(allow - covered)
        if missed:
            print(f"[MISS] {len(missed)} linked source(s) fell outside every "
                  f"stratum wave:")
            for m in missed:
                print(f"  {m}")
        else:
            print("[cover] every linked source landed in a wave")
    return 1 if fails else 0


if __name__ == "__main__":
    main(sys.argv)
