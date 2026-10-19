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
       nonlinear_build.py <strata.txt> <src root> <out dir> --srcs <file> [<cc> <cflags...>]

For each stage k it writes <out>/stage_<k>.list and compiles the listed
sources to <out>/stage_<k>/*.o with -j parallelism (default nproc).
--srcs restricts compilation to the paths in <file> (one per line), so the
waves cover exactly the objects the real kernel links, not host-tier test or
bench programs that also live under the source root.
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
        return sorted(x for x in p.iterdir()
                      if x.suffix == ".c"
                      and str(x.relative_to(root)) not in pinned
                      and str(x.relative_to(root))[:-2] not in pinned)
    for cand in (root / (name + ".c"), root / name):
        if cand.suffix == ".c" and cand.is_file():
            return [cand]
    return []


def main(argv):
    strata, root, out = pathlib.Path(argv[1]), pathlib.Path(argv[2]), \
        pathlib.Path(argv[3])
    args = argv[4:]
    allow = None
    if args[:1] == ["--srcs"]:
        allow = {str(pathlib.Path(l.strip()).resolve())
                 for l in pathlib.Path(args[1]).read_text().splitlines()
                 if l.strip()}
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
    for ring in sorted(stages):
        # ring k is one build wave: every source compiles in parallel, and
        # the wave completes before ring k+1 starts — the scaffold's
        # guarantee is that all of ring k's dependencies live below it.
        srcs = []
        for mod in sorted(stages[ring]):
            srcs += module_sources(root, mod, assign_)
        if allow is not None:
            srcs = [s for s in srcs if str(s.resolve()) in allow]
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
        it = iter(srcs)
        inflight = 0
        pending = list(srcs)
        while pending or running:
            while pending and inflight < lanes:
                src = pending.pop(0)
                # mirror the source path in the object name: two modules can
                # carry same-named files, and a flat name would silently
                # overwrite the earlier object.
                oname = "__".join(src.relative_to(root).parts) + ".o"
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
        print(f"[stage {ring}] {len(stages[ring])} modules, {len(srcs)} "
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
