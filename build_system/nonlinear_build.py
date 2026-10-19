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

For each stage k it writes <out>/stage_<k>.list and compiles the listed
sources to <out>/stage_<k>/*.o with -j parallelism (default nproc).
"""
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_lattice_routing import assign, parse_strata  # noqa: E402


def module_sources(root, name):
    """a strata path covers either one file (foo or foo.c) or a directory."""
    p = root / name
    if p.is_dir():
        return sorted(x for x in p.iterdir() if x.suffix == ".c")
    for cand in (root / (name + ".c"), root / name):
        if cand.suffix == ".c" and cand.is_file():
            return [cand]
    return []


def main(argv):
    strata, root, out = pathlib.Path(argv[1]), pathlib.Path(argv[2]), \
        pathlib.Path(argv[3])
    cc = argv[4] if len(argv) > 4 else "gcc"
    cflags = argv[5:] if len(argv) > 5 else ["-std=c11", "-O2", "-c"]

    assign_ = assign(parse_strata(strata))
    stages = {}
    for path, (cell, ring) in assign_.items():
        stages.setdefault(ring, []).append(path)

    out.mkdir(parents=True, exist_ok=True)
    total, fails = 0, 0
    for ring in sorted(stages):
        # ring k is one build wave: every source compiles in parallel, and
        # the wave completes before ring k+1 starts — the scaffold's
        # guarantee is that all of ring k's dependencies live below it.
        srcs = []
        for mod in sorted(stages[ring]):
            srcs += module_sources(root, mod)
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
                running.append((src, subprocess.Popen(
                    [cc, *cflags, "-o",
                     str(sdir / (src.name + ".o")), str(src)],
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
    return 1 if fails else 0


if __name__ == "__main__":
    main(sys.argv)
