#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""S3: turn same-base fine-tunes into adapters on their shared trunk.

For each cell in a tissue (tiamat_swarm.json "tissues"): delta = W_cell -
W_trunk for every linear layer at or above the split layer, truncated SVD
to rank r, written as a LoRA (A = r x in, B = out x r). Layers below the
split get no adapter, so every cell in the tissue computes exactly the same
bits there and can share that pass and its KV cache (cross-wiring).

The quality gate compares trunk+adapter against the full fine-tune on
held-out prompts: top-1 next-token agreement and mean KL divergence. The
script searches ranks and split layers and keeps the highest split (most
sharing) that passes at the smallest rank. A cell that passes nowhere is
reported FULL: it ships as full weights in a pack, never as a bad adapter.

Embedding and output-matrix deltas cannot be adapters (zt_lora refuses
them); their size is reported, and the gate measures what dropping them
costs.

Three kinds of cell need no search:
- an upstream LoRA (its src holds adapter_config.json): already an adapter.
  It is checked (LORA, no saved modules, every tensor on a trunk linear of
  the right shape, its base the tissue's model) and copied as it is; the
  report gives its effect on the trunk, since there is no full model to
  compare with. UPSTREAM_ADAPTER.
- a cell published only as a quantised GGUF: its delta from the trunk would
  be mostly quantisation noise, so it ships whole. FULL.
- a cell whose weights or config cannot be read: ERROR with the reason; the
  other cells still run, and the report is written after every cell.

Every adapter in the report carries the SHA-256 and size of its files, for
build_disc_models.sh (S9) and the disc's golden list.

    python3 extract_adapters.py --models /srv/tiamat/models --out /srv/tiamat/adapters \\
        --tissue Qwen/Qwen2.5-1.5B-Instruct [--prompts prompts.jsonl]

Then convert each PEFT adapter for the engine with llama.cpp:
    python3 convert_lora_to_gguf.py --base <trunk dir> --outtype f16 <adapter dir>
"""
import argparse
import hashlib
import json
import math
import shutil
import pathlib
import sys

import torch
from safetensors.torch import load_file, save_file
from transformers import AutoModelForCausalLM, AutoTokenizer

HERE = pathlib.Path(__file__).resolve().parent
CFG = json.loads((HERE.parent / "tiamat_swarm.json").read_text())
MAN = json.loads((HERE.parents[2] / "docs/release/swarm_model_manifest.json").read_text())
BY_NAME = {m["dragon_name"]: m for m in MAN["models"]}
TARGETS = ("q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj")

DEFAULT_PROMPTS = [
    "Fix the bug in this Python function:\ndef mean(xs):\n    return sum(xs) / len(xs) + 1\n",
    "Review this code for security problems:\nimport os\nos.system('rm -rf ' + user_input)\n",
    "Is this argument valid? All birds fly. Penguins are birds. So penguins fly.",
    "Solve: if 3x + 7 = 22, what is x?",
    "Prove that the sum of two even numbers is even.",
    "Write a SQL query that counts orders per customer in table orders(customer_id, id).",
    "Under the EU AI Act, what is a high-risk AI system?",
    "Summarise the main claim: The bridge closed because inspectors found cracks in two supports.",
    "Extract the people and places: Ada Lovelace met Charles Babbage in London in 1833.",
    "Which statement contradicts the other? A: The shop opens at 9. B: The shop is closed all morning.",
    "Explain what a hash table is in two sentences.",
    "What is 17 times 23?",
    "Translate to French: The library opens tomorrow.",
    "List three risks of storing passwords in plain text.",
    "Refactor this loop into a list comprehension:\nout = []\nfor x in xs:\n    if x > 0:\n        out.append(x * 2)\n",
    "Given P implies Q and not Q, what follows about P?",
]


def kind(src):
    """What a cell's src folder holds: upstream_lora, gguf_only, weights, or why it is unreadable."""
    if (src / "adapter_config.json").is_file():
        return "upstream_lora", None
    st = sorted(src.glob("*.safetensors"))
    if not st:
        if sorted(src.glob("*.gguf")):
            return "gguf_only", None
        return "error", f"no weights in {src} (no *.safetensors, *.gguf or adapter_config.json)"
    if not (src / "config.json").is_file():
        return "error", f"{src} has weights but no config.json"
    if len(st) > 1 and not (src / "model.safetensors.index.json").is_file():
        return "error", f"{src} has {len(st)} weight shards but no model.safetensors.index.json"
    return "weights", None


def load(path, dtype):
    src = pathlib.Path(path) / "src"
    k, why = kind(src)
    if k != "weights":
        raise RuntimeError(why or f"{src} holds {k}, not full weights")
    return AutoModelForCausalLM.from_pretrained(str(src), torch_dtype=dtype, low_cpu_mem_usage=True)


def file_sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 22), b""):
            h.update(chunk)
    return h.hexdigest()


def files_of(d):
    return [{"path": f.name, "size": f.stat().st_size, "sha256": file_sha(f)}
            for f in sorted(d.iterdir()) if f.is_file()]


def linears(model):
    out = {}
    for name, mod in model.named_modules():
        if isinstance(mod, torch.nn.Linear) and name.split(".")[-1] in TARGETS and ".layers." in name:
            out[name] = mod
    return out


def layer_of(name):
    parts = name.split(".")
    return int(parts[parts.index("layers") + 1])


def factor(delta, rank):
    u, s, vh = torch.linalg.svd(delta.float(), full_matrices=False)
    r = min(rank, s.numel())
    sq = s[:r].sqrt()
    return (u[:, :r] * sq).contiguous(), (sq[:, None] * vh[:r]).contiguous()  # B, A


@torch.no_grad()
def gate(trunk, cell, adapters, tok, prompts, dev):
    """Apply adapters to a copy of the trunk's linears via hooks; compare with the cell."""
    hooks = []
    mods = linears(trunk)
    for name, (b, a) in adapters.items():
        m = mods[name]
        bb, aa = b.to(dev, m.weight.dtype), a.to(dev, m.weight.dtype)
        hooks.append(m.register_forward_hook(lambda mod, inp, out, bb=bb, aa=aa: out + (inp[0] @ aa.T) @ bb.T))
    agree = n = 0
    kl = 0.0
    for p in prompts:
        ids = tok(p, return_tensors="pt").input_ids.to(dev)
        lt = trunk(ids).logits.float().log_softmax(-1)
        lc = cell(ids).logits.float().log_softmax(-1)
        agree += (lt.argmax(-1) == lc.argmax(-1)).sum().item()
        n += ids.numel()
        kl += torch.nn.functional.kl_div(lt, lc, log_target=True, reduction="sum").item()
    for h in hooks:
        h.remove()
    return agree / n, kl / n


def same_model(base, tissue):
    """An adapter's recorded base is the tissue's model, or a quantisation of it."""
    b, t = base.lower(), tissue.split("/")[-1].lower()
    return t in b, any(q in b for q in ("bnb-4bit", "4bit", "8bit", "awq", "gptq", "gguf"))


def upstream_lora(src, tissue, mods):
    """Check an upstream PEFT LoRA against the trunk; return {module: (B * scale, A)} and its facts."""
    cfg = json.loads((src / "adapter_config.json").read_text())
    if cfg.get("peft_type") != "LORA":
        raise RuntimeError(f"peft_type is {cfg.get('peft_type')!r}, not LORA")
    if cfg.get("modules_to_save"):
        raise RuntimeError(f"it saves whole modules {cfg['modules_to_save']}, which an adapter cannot carry")
    if cfg.get("rank_pattern") or cfg.get("alpha_pattern"):
        raise RuntimeError("per-module ranks or alphas are not supported")
    if cfg.get("bias", "none") != "none":
        raise RuntimeError(f"bias is {cfg['bias']!r}; only 'none' is supported")
    base = cfg.get("base_model_name_or_path") or ""
    match, quantised = same_model(base, tissue)
    if not match:
        raise RuntimeError(f"its base is {base!r}, not {tissue}")
    r, alpha = int(cfg["r"]), float(cfg["lora_alpha"])
    scale = alpha / math.sqrt(r) if cfg.get("use_rslora") else alpha / r
    w = load_file(str(src / "adapter_model.safetensors"))
    pairs = {}
    for key, t in w.items():
        k = key.removeprefix("base_model.model.").replace(".default", "")
        for part in ("lora_A", "lora_B"):
            if k.endswith(f".{part}.weight"):
                pairs.setdefault(k[: -len(f".{part}.weight")], {})[part] = t
                break
        else:
            raise RuntimeError(f"tensor {key} is not a LoRA factor")
    ad = {}
    for name, ab in sorted(pairs.items()):
        if name not in mods:
            raise RuntimeError(f"{name} is not one of the trunk's adapter targets")
        if set(ab) != {"lora_A", "lora_B"}:
            raise RuntimeError(f"{name} lacks one of its two factors")
        a, b, wt = ab["lora_A"], ab["lora_B"], mods[name].weight
        if a.shape != (r, wt.shape[1]) or b.shape != (wt.shape[0], r):
            raise RuntimeError(f"{name}: factors {tuple(a.shape)} x {tuple(b.shape)} do not fit {tuple(wt.shape)} at rank {r}")
        if not (torch.isfinite(a).all() and torch.isfinite(b).all()):
            raise RuntimeError(f"{name}: a factor holds NaN or infinity")
        ad[name] = (b.float() * scale, a.float())
    if not ad:
        raise RuntimeError("the adapter holds no LoRA factors")
    facts = {"upstream_base": base, "trained_on_quantised_base": quantised, "rank": r, "lora_alpha": alpha,
             "use_rslora": bool(cfg.get("use_rslora")), "modules": len(ad),
             "layers": sorted({layer_of(n) for n in ad})}
    return ad, facts


@torch.no_grad()
def effect(trunk, adapters, tok, prompts, dev):
    """Share of next-token choices the adapter changes on the trunk, and whether its logits stay finite."""
    plain = [trunk(tok(p, return_tensors="pt").input_ids.to(dev)).logits.argmax(-1) for p in prompts]
    mods, hooks = linears(trunk), []
    for name, (b, a) in adapters.items():
        m = mods[name]
        bb, aa = b.to(dev, m.weight.dtype), a.to(dev, m.weight.dtype)
        hooks.append(m.register_forward_hook(lambda mod, inp, out, bb=bb, aa=aa: out + (inp[0] @ aa.T) @ bb.T))
    changed = n = 0
    finite = True
    try:
        for p, ref in zip(prompts, plain):
            lg = trunk(tok(p, return_tensors="pt").input_ids.to(dev)).logits
            finite = finite and bool(torch.isfinite(lg).all())
            changed += (lg.argmax(-1) != ref).sum().item()
            n += ref.numel()
    finally:
        for h in hooks:
            h.remove()
    return changed / n, finite


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--tissue", required=True)
    ap.add_argument("--prompts")
    ap.add_argument("--ranks", default="16,32,64,128")
    ap.add_argument("--min-agree", type=float, default=0.97)
    ap.add_argument("--max-kl", type=float, default=0.05)
    ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    ap.add_argument("--cells", nargs="*", help="only these cells of the tissue")
    a = ap.parse_args()
    t = CFG["tissues"][a.tissue]
    dev, dtype = a.device, torch.bfloat16
    prompts = [json.loads(l)["prompt"] for l in open(a.prompts)] if a.prompts else DEFAULT_PROMPTS
    root = pathlib.Path(a.models)
    trunk = load(root / t["trunk"], dtype).to(dev).eval()
    tok = AutoTokenizer.from_pretrained(str(root / t["trunk"] / "src"))
    n_layer = trunk.config.num_hidden_layers
    tm = linears(trunk)
    report = {"tissue": a.tissue, "trunk": t["trunk"], "cells": {}}
    out_dir = pathlib.Path(a.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / f"report_{t['trunk']}.json"  # one report per tissue, named for its trunk

    def write_report():
        splits = [c["split_layer"] for c in report["cells"].values() if c["result"] == "ADAPTER"]
        report["shared_split_layer"] = min(splits) if splits else None
        report_path.write_text(json.dumps(report, indent=2) + "\n")

    for name in t["cells"]:
        if a.cells and name not in a.cells:
            continue
        src = root / name / "src"
        try:
            entry = one_cell(a, name, src, trunk, tm, tok, prompts, n_layer, dev, dtype, out_dir)
        except Exception as e:  # a cell that cannot be read is reported; the others still run
            entry = {"result": "ERROR", "why": f"{type(e).__name__}: {e}"}
        print(f"{name}: {entry['result']}" + (f" ({entry['why']})" if "why" in entry else ""), flush=True)
        report["cells"][name] = entry
        write_report()
        if dev == "cuda":
            torch.cuda.empty_cache()
    write_report()
    print(json.dumps(report, indent=2))
    return 3 if any(c["result"] == "ERROR" for c in report["cells"].values()) else 0


def one_cell(a, name, src, trunk, tm, tok, prompts, n_layer, dev, dtype, out_dir):
    k, why = kind(src)
    d = out_dir / name
    if k == "error":
        return {"result": "ERROR", "why": why}
    if k == "gguf_only":
        return {"result": "FULL", "why": "published only as a quantised GGUF; its delta from the trunk "
                                         "would be mostly quantisation noise, so it ships whole"}
    if k == "upstream_lora":
        ad, facts = upstream_lora(src, a.tissue, tm)
        changed, finite = effect(trunk, ad, tok, prompts, dev)
        if not finite:
            return {"result": "ERROR", "why": "the trunk's logits are not finite with this adapter", **facts}
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
        for f in ("adapter_config.json", "adapter_model.safetensors"):
            shutil.copyfile(src / f, d / f)
        return {"result": "UPSTREAM_ADAPTER", **facts, "next_token_changed": changed, "files": files_of(d)}
    cell = load(src.parent, dtype).to(dev).eval()
    try:
        cm = linears(cell)
        if set(cm) != set(tm) or any(cm[x].weight.shape != tm[x].weight.shape for x in tm):
            return {"result": "ERROR", "why": "its linear layers do not match the trunk's"}
        emb = (cell.get_input_embeddings().weight - trunk.get_input_embeddings().weight).norm().item()
        deltas = {x: (cm[x].weight - tm[x].weight).detach() for x in tm}
        best = None
        for split in range(n_layer // 2, -1, -max(1, n_layer // 8)):
            for rank in map(int, a.ranks.split(",")):
                ad = {x: factor(dl, rank) for x, dl in deltas.items() if layer_of(x) >= split}
                ag, kl = gate(trunk, cell, ad, tok, prompts, dev)
                print(f"{name} split={split} rank={rank} agree={ag:.4f} kl={kl:.4f}", flush=True)
                if ag >= a.min_agree and kl <= a.max_kl:
                    best = (split, rank, ag, kl, ad)
                    break
            if best:
                break
    finally:
        del cell
    entry = {"embedding_delta_norm": emb}
    if not best:
        return entry | {"result": "FULL", "why": f"no split and rank reach top-1 {a.min_agree} and KL {a.max_kl}"}
    split, rank, ag, kl, ad = best
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)
    tensors = {}
    for x, (b, aa) in ad.items():
        key = "base_model.model." + x
        tensors[key + ".lora_A.weight"] = aa.to(torch.float16).cpu()
        tensors[key + ".lora_B.weight"] = b.to(torch.float16).cpu()
    save_file(tensors, str(d / "adapter_model.safetensors"))
    (d / "adapter_config.json").write_text(json.dumps({
        "peft_type": "LORA", "r": rank, "lora_alpha": rank, "target_modules": list(TARGETS),
        "layers_to_transform": list(range(split, n_layer)), "bias": "none",
        "base_model_name_or_path": a.tissue}, indent=2))
    return entry | {"result": "ADAPTER", "split_layer": split, "rank": rank, "top1_agree": ag, "kl": kl,
                    "files": files_of(d)}


if __name__ == "__main__":
    sys.exit(main())
