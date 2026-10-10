#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Reproducible tensor benchmark: the ZXV integer engine vs llama.cpp on one GGUF.

1. Runs bench/tensor/zt_bench (the integer engine, kernel/src/tensor) on MODEL:
   prefill tokens/s, greedy generation tokens/s, the generated ids and text.
2. If llama.cpp is on PATH:
   - llama-bench on the same model, same prompt length and N (tokens/s), and
   - llama-completion (or llama-cli) greedy (--temp 0, --top-k 1) on the same
     prompt, and whether its first N tokens' text equals the engine's text.
3. Prints a summary and, with --json FILE, writes it as JSON.

Nothing is downloaded. Nothing is claimed: the numbers belong to the machine,
build and model you ran on, and are printed with those details.

The match check compares decoded TEXT, byte for byte: llama.cpp's command-line
tools do not print token ids. Equal text for N greedy tokens is strong
evidence that the token sequences agree; a mismatch is reported with the
offset of the first differing byte.

Usage:
  run_tensor_bench.py --zt-bench PATH --model MODEL.gguf [-n 32] [--prompt TEXT]
                      [--threads T] [--json OUT.json]
  run_tensor_bench.py --self-test       (offline checks of the output parsers)
"""
import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile


def parse_zt(out):
    """Parse zt_bench's key=value lines."""
    r = {}
    for line in out.splitlines():
        if line.startswith("zt.") and "=" in line:
            k, v = line[3:].split("=", 1)
            r[k] = v
    for k in ("n_prompt", "n_gen"):
        if k in r:
            r[k] = int(r[k])
    for k in ("prefill_s", "prefill_tps", "gen_s", "gen_tps"):
        if k in r:
            r[k] = float(r[k])
    r["ids"] = [int(x) for x in r.get("ids", "").split(",") if x]
    return r


def parse_llama_bench(out):
    """Parse `llama-bench -o json`: a list of tests with n_prompt, n_gen, avg_ts."""
    start = out.find("[")
    if start < 0:
        return []
    try:
        data = json.loads(out[start:])
    except json.JSONDecodeError:
        return []
    res = []
    for t in data:
        res.append(
            {
                "n_prompt": t.get("n_prompt", 0),
                "n_gen": t.get("n_gen", 0),
                "avg_ts": t.get("avg_ts"),
                "stddev_ts": t.get("stddev_ts"),
                "backends": t.get("backends", t.get("backend")),
                "build": t.get("build_commit"),
            }
        )
    return res


def first_diff(a, b):
    """Offset of the first differing byte over the shorter length, or -1."""
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return i
    return -1


def compare_text(ours, theirs):
    """ours: engine bytes for N tokens; theirs: llama.cpp continuation bytes."""
    if ours == theirs[: len(ours)]:
        return {"match": True, "first_diff": -1}
    if theirs == ours[: len(theirs)]:
        # llama.cpp stopped earlier (end of text); everything it produced agrees.
        return {"match": True, "first_diff": -1, "note": "llama.cpp output is shorter"}
    return {"match": False, "first_diff": first_diff(ours, theirs)}


def run(cmd, timeout):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return p.returncode, p.stdout, p.stderr


def llama_greedy(model, prompt, n, threads, timeout):
    """Greedy continuation bytes from llama.cpp, or (None, why)."""
    common = ["-m", model, "-p", prompt, "-n", str(n), "--temp", "0", "--top-k", "1",
              "-s", "0", "--no-display-prompt", "--no-warmup"]
    if threads:
        common += ["-t", str(threads)]
    tried = []
    for exe, extra in (("llama-completion", []), ("llama-cli", ["-no-cnv"])):
        path = shutil.which(exe)
        if not path:
            continue
        rc, out, err = run([path] + common + extra, timeout)
        tried.append(exe)
        if rc == 0:
            return out, exe
    return None, ("not on PATH" if not tried else "failed: " + ", ".join(tried))


def self_test():
    zt = "zt.n_prompt=6\nzt.prefill_s=0.5\nzt.prefill_tps=12\nzt.n_gen=3\nzt.gen_s=1.5\n" \
         "zt.gen_tps=2\nzt.stop=max\nzt.ids=11,22,33\n"
    r = parse_zt(zt)
    assert r["n_prompt"] == 6 and r["ids"] == [11, 22, 33] and r["gen_tps"] == 2.0, r
    lb = 'build: 1234\n[{"n_prompt": 6, "n_gen": 0, "avg_ts": 100.5, "stddev_ts": 1.0},' \
         ' {"n_prompt": 0, "n_gen": 32, "avg_ts": 20.25, "stddev_ts": 0.5}]'
    b = parse_llama_bench(lb)
    assert len(b) == 2 and b[1]["avg_ts"] == 20.25, b
    assert compare_text(b" Paris.", b" Paris. It")["match"]
    assert compare_text(b" Paris. It", b" Paris.")["match"]
    c = compare_text(b" Paris", b" Lyon")
    assert not c["match"] and c["first_diff"] == 1, c
    print("[PASS] run_tensor_bench.py self-test (parsers and text comparison)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--zt-bench")
    ap.add_argument("--model")
    ap.add_argument("-n", type=int, default=32)
    ap.add_argument("--prompt", default="The capital of France is")
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--timeout", type=int, default=3600)
    ap.add_argument("--json")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.zt_bench or not a.model:
        ap.error("--zt-bench and --model are required")
    if not os.path.isfile(a.model):
        ap.error("model not found: %s (this script never downloads one)" % a.model)

    summary = {
        "model": os.path.abspath(a.model),
        "model_bytes": os.path.getsize(a.model),
        "prompt": a.prompt,
        "n": a.n,
        "machine": {"platform": platform.platform(), "machine": platform.machine(),
                    "processor": platform.processor(), "cpus": os.cpu_count()},
        "note": "measured on this machine only; no result is claimed beyond it",
    }
    with tempfile.TemporaryDirectory() as td:
        txt = os.path.join(td, "zt.txt")
        rc, out, err = run([a.zt_bench, a.model, "--prompt", a.prompt, "-n", str(a.n),
                            "--text", txt], a.timeout)
        if rc != 0:
            sys.stderr.write(err.decode(errors="replace"))
            print("[FAIL] zt_bench exited %d on this model" % rc)
            return 1
        zt = parse_zt(out.decode())
        with open(txt, "rb") as f:
            zt_text = f.read()
    summary["zt"] = {k: zt[k] for k in zt if k != "ids"}
    summary["zt"]["ids"] = zt["ids"]
    summary["zt"]["text"] = zt_text.decode("utf-8", errors="replace")
    print("ZXV integer engine (kernel/src/tensor):")
    print("  prompt tokens %d, prefill %.2f tokens/s" % (zt["n_prompt"], zt["prefill_tps"]))
    print("  generated %d tokens greedily, %.2f tokens/s (stop: %s)"
          % (zt["n_gen"], zt["gen_tps"], zt.get("stop")))
    print("  text: %r" % zt_text.decode("utf-8", errors="replace"))

    lb = shutil.which("llama-bench")
    if lb:
        cmd = [lb, "-m", a.model, "-p", str(zt["n_prompt"]), "-n", str(a.n), "-o", "json"]
        if a.threads:
            cmd += ["-t", str(a.threads)]
        rc, out, err = run(cmd, a.timeout)
        tests = parse_llama_bench(out.decode(errors="replace")) if rc == 0 else []
        summary["llama_bench"] = tests or {"error": "llama-bench exited %d" % rc}
        print("llama-bench:")
        for t in tests:
            kind = "prompt %d" % t["n_prompt"] if t["n_prompt"] else "generate %d" % t["n_gen"]
            print("  %-12s %.2f tokens/s (+/- %.2f)" % (kind, t["avg_ts"] or 0, t["stddev_ts"] or 0))
        if not tests:
            print("  llama-bench failed (exit %d)" % rc)
    else:
        summary["llama_bench"] = "not on PATH"
        print("llama-bench: not on PATH, skipped")

    theirs, how = llama_greedy(a.model, a.prompt, a.n, a.threads, a.timeout)
    if theirs is None:
        summary["greedy_match"] = "llama.cpp %s, skipped" % how
        print("greedy match: llama-completion/llama-cli %s, skipped" % how)
    else:
        c = compare_text(zt_text, theirs)
        c["tool"] = how
        c["llama_text"] = theirs.decode("utf-8", errors="replace")
        summary["greedy_match"] = c
        if c["match"]:
            print("greedy match: the first %d greedy tokens give the same text as %s%s"
                  % (zt["n_gen"], how, " (" + c["note"] + ")" if "note" in c else ""))
        else:
            print("greedy match: NO -- the texts differ from byte %d" % c["first_diff"])
            print("  %s: %r" % (how, c["llama_text"]))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(summary, f, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
