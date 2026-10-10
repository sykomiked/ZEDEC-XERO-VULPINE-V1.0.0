#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Real-model test: Qwen2.5-1.5B-Instruct Q4_K_M through the ZXV integer engine.

  1. Fetch the GGUF into a cache directory OUTSIDE the repository (default
     ~/.cache/zxv/models), unless it is already there. The file is checked
     against its size and its SHA-256: the hash Hugging Face publishes for the
     LFS object (from the repository tree API), and --sha256 if given. A file
     that fails either check is deleted, never used.
  2. Run zt_bench (one or more builds, e.g. x86_64 and an arm64 build under
     qemu) with greedy decoding on a fixed prompt set wrapped in Qwen's chat
     template, with every kernel set each build offers.
  3. Report, per run: tokens/s (prefill and generation), the generated ids and
     the FNV-1a hash of every logit vector. All runs of a prompt must agree
     bit for bit. Each answer is also checked for an expected substring.

Model weights never enter git: the cache path is refused if it is inside the
repository. Numbers are measurements of the machine they ran on; nothing else.

Usage:
  real_model_test.py --bench zt_bench [--bench "qemu-aarch64 zt_bench_arm64" ...]
                     [--model PATH] [--sha256 HEX] [-n 64] [--threads T]
                     [--json OUT.json]
"""
import argparse
import hashlib
import json
import os
import platform
import shlex
import subprocess
import sys
import tempfile
import urllib.request

REPO = "Qwen/Qwen2.5-1.5B-Instruct-GGUF"
FILE = "qwen2.5-1.5b-instruct-q4_k_m.gguf"
SIZE = 1117320736

# Fixed prompts: (question, substrings any of which counts as correct; case-insensitive).
PROMPTS = [
    ("What is the capital of France? Answer in one word.", ["paris"]),
    ("What is 17 + 25? Answer with the number only.", ["42"]),
    ("How many days are in a leap year? Answer with the number only.", ["366"]),
    ("Which planet is known as the Red Planet? Answer in one word.", ["mars"]),
    ("Translate 'thank you' into Spanish. Answer with the translation only.", ["gracias"]),
    ("What is the chemical symbol for gold? Answer with the symbol only.", ["au"]),
    ("Write a Python expression that reverses a string s. Answer with code only.", ["[::-1]"]),
    ("Who wrote 'Romeo and Juliet'? Answer with the name only.", ["shakespeare"]),
]

SYSTEM = "You are a helpful assistant."


def chat(q):
    return (f"<|im_start|>system\n{SYSTEM}<|im_end|>\n"
            f"<|im_start|>user\n{q}<|im_end|>\n<|im_start|>assistant\n")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def hub_sha256():
    url = f"https://huggingface.co/api/models/{REPO}/tree/main"
    with urllib.request.urlopen(url, timeout=60) as r:
        for e in json.load(r):
            if e.get("path") == FILE:
                lfs = e.get("lfs") or {}
                return lfs.get("oid") or lfs.get("sha256"), e.get("size")
    raise SystemExit(f"{FILE} not listed in {REPO}")


def fetch(path, pinned):
    want, size = hub_sha256()
    if size != SIZE:
        raise SystemExit(f"hub reports size {size}, expected {SIZE}")
    if pinned and pinned.lower() != want:
        raise SystemExit(f"hub SHA-256 {want} differs from --sha256 {pinned}")
    if not os.path.exists(path):
        url = f"https://huggingface.co/{REPO}/resolve/main/{FILE}"
        tmp = path + ".part"
        print(f"downloading {url}", file=sys.stderr)
        subprocess.run(["curl", "-fL", "--retry", "4", "-C", "-", "-o", tmp, url], check=True)
        os.replace(tmp, path)
    got_size = os.path.getsize(path)
    got = sha256_file(path)
    if got_size != SIZE or got != want:
        os.remove(path)
        raise SystemExit(f"{path}: size {got_size} sha256 {got}; expected {SIZE} {want}. Deleted.")
    return got


def run(bench, model, prompt, n, kern, threads):
    with tempfile.NamedTemporaryFile(suffix=".txt") as t:
        cmd = shlex.split(bench) + [model, "--special", "--prompt", prompt, "-n", str(n),
                                    "--kern", kern, "--threads", str(threads), "--text", t.name]
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode != 0:
            return {"error": p.stderr.strip(), "rc": p.returncode}
        text = open(t.name, "rb").read()
    kv = {}
    for line in p.stdout.splitlines():
        if line.startswith("zt.") and "=" in line:
            k, v = line[3:].split("=", 1)
            kv[k] = v
    kv["text"] = text.decode(errors="replace").strip()
    return kv


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench", action="append", required=True)
    ap.add_argument("--model")
    ap.add_argument("--cache", default=os.path.expanduser("~/.cache/zxv/models"))
    ap.add_argument("--sha256")
    ap.add_argument("-n", type=int, default=64)
    ap.add_argument("--threads", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--kern", action="append", help="limit kernel sets (default: all available)")
    ap.add_argument("--json")
    ap.add_argument("--offline", action="store_true",
                    help="no network: --model and --sha256 are required and must match")
    a = ap.parse_args()

    repo = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", ".."))
    model = a.model or os.path.join(a.cache, FILE)
    if os.path.realpath(model).startswith(repo + os.sep):
        raise SystemExit("refusing to keep model weights inside the repository")
    os.makedirs(os.path.dirname(model), exist_ok=True)
    if a.offline:
        if not (a.model and a.sha256):
            raise SystemExit("--offline needs --model and --sha256")
        sha = sha256_file(model)
        if sha != a.sha256.lower():
            raise SystemExit(f"{model}: sha256 {sha} != --sha256 {a.sha256}")
    else:
        sha = fetch(model, a.sha256)
    print(f"model {model}\nsha256 {sha}\nhost {platform.machine()} {platform.processor()} "
          f"cpus={os.cpu_count()}")

    results, ok_all = [], True
    for qi, (q, expect) in enumerate(PROMPTS):
        runs = []
        for b in a.bench:
            for k in (a.kern or ["c", "avx2", "avx512", "neon"]):
                r = run(b, model, chat(q), a.n, k, a.threads)
                if "error" in r:
                    if "not available" in r["error"]:
                        continue
                    raise SystemExit(f"{b} --kern {k}: {r['error']}")
                r["bench"], r["kern"] = b, k
                runs.append(r)
        same = len({(r["ids"], r["logits_fnv"]) for r in runs}) == 1
        correct = any(e in runs[0]["text"].lower() for e in expect)
        ok_all &= same
        results.append({"prompt": q, "expect": expect, "correct": correct, "bit_identical": same,
                        "answer": runs[0]["text"], "runs": runs})
        print(f"\n[{qi + 1}] {q}\n    answer: {runs[0]['text']!r}\n    correct={correct} "
              f"bit_identical={same} across {len(runs)} runs")
        for r in runs:
            print(f"    {r['bench'].split()[-1]:<18} {r['kern']:<7} prefill {float(r['prefill_tps']):8.2f} "
                  f"tok/s  gen {float(r['gen_tps']):7.2f} tok/s  n_gen={r['n_gen']} "
                  f"fnv={r['logits_fnv']}")
    n_ok = sum(r["correct"] for r in results)
    print(f"\nquality: {n_ok}/{len(results)} correct; bit-identical on every prompt: {ok_all}")
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"model": model, "sha256": sha, "machine": platform.machine(),
                       "threads": a.threads, "n": a.n, "results": results}, f, indent=1)
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
