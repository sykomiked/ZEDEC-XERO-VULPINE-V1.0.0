#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""S8: measure what cross-wiring a tissue saves, honestly.

Two arms on the same prompts, same GPU, same precision:
  separate     every cell is its own full model; each runs every prompt.
  cross-wired  one trunk; layers below the tissue's split run once per
               prompt; each cell then runs only the layers above the split,
               with its adapter (extract_adapters.py output).
Reported per arm: resident weight bytes, peak GPU memory, wall time,
multiply-adds (counted from the shapes), and per cell the top-1 agreement
of cross-wired against separate plus, with --data, four-valued answer
accuracy. The saving is reported with the quality it cost; a loss is
printed as a loss. Rules fixed before running (docs/release/
SWARM_MODEL_VERIFICATION.md, "Measuring cross-wiring").

    python3 bench_crosswire.py --models /srv/tiamat/models --adapters /srv/tiamat/adapters \\
        --tissue Qwen/Qwen2.5-1.5B-Instruct [--data tfbn.jsonl --n 400] --out bench.json
"""
import argparse
import json
import pathlib
import time

import torch
from safetensors.torch import load_file
from transformers import AutoModelForCausalLM, AutoTokenizer

HERE = pathlib.Path(__file__).resolve().parent
CFG = json.loads((HERE.parent / "tiamat_swarm.json").read_text())
WORDS = ["TRUE", "FALSE", "BOTH", "NEITHER"]


def mbytes(model):
    return sum(p.numel() * p.element_size() for p in model.parameters())


def macs_per_token(cfg, layers):
    d, f = cfg.hidden_size, cfg.intermediate_size
    kv = d // cfg.num_attention_heads * cfg.num_key_value_heads
    return layers * (d * d * 2 + d * kv * 2 + 3 * d * f)


def run_layers(m, h, pos, pe, lo, hi):
    for layer in m.model.layers[lo:hi]:
        o = layer(h, position_ids=pos, position_embeddings=pe)
        h = o[0] if isinstance(o, tuple) else o
    return h


def adapter_hooks(model, path, dev):
    t = load_file(str(path / "adapter_model.safetensors"))
    mods = dict(model.named_modules())
    hooks = []
    for k in t:
        if not k.endswith(".lora_A.weight"):
            continue
        name = k[len("base_model.model."):-len(".lora_A.weight")]
        a = t[k].to(dev, torch.bfloat16)
        b = t[k.replace("lora_A", "lora_B")].to(dev, torch.bfloat16)
        hooks.append(mods[name].register_forward_hook(lambda mod, i, o, a=a, b=b: o + (i[0] @ a.T) @ b.T))
    return hooks


def answer(logits, tok):
    ids = [tok(" " + w, add_special_tokens=False).input_ids[0] for w in WORDS]
    return WORDS[int(logits[ids].argmax())]


@torch.no_grad()
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", required=True)
    ap.add_argument("--adapters", required=True)
    ap.add_argument("--tissue", required=True)
    ap.add_argument("--data")
    ap.add_argument("--n", type=int, default=200)
    ap.add_argument("--out", required=True)
    ap.add_argument("--device", default="cuda")
    a = ap.parse_args()
    dev = a.device
    t = CFG["tissues"][a.tissue]
    root, adroot = pathlib.Path(a.models), pathlib.Path(a.adapters)
    rep = json.loads((adroot / f"report_{t['trunk']}.json").read_text())
    split = rep["shared_split_layer"]
    cells = [c for c in t["cells"] if rep["cells"][c]["result"] == "ADAPTER"]
    tok = AutoTokenizer.from_pretrained(str(root / t["trunk"] / "src"))
    if a.data:
        items = [json.loads(l) for l in open(a.data)][: a.n]
        prompts = [d["prompt"] for d in items]
    else:
        items, prompts = None, [f"Question {i}: explain step {i} of sorting a list." for i in range(a.n)]
    load = lambda p: AutoModelForCausalLM.from_pretrained(str(p / "src"), torch_dtype=torch.bfloat16).to(dev).eval()

    # arm 1: separate
    torch.cuda.reset_peak_memory_stats()
    sep, sep_bytes, sep_compute, t0 = {}, 0, 0.0, time.time()
    for c in cells:
        m = load(root / c)
        sep_bytes += mbytes(m)
        tc = time.time()
        sep[c] = [m(tok(p, return_tensors="pt").input_ids.to(dev)).logits[0, -1].float().cpu() for p in prompts]
        sep_compute += time.time() - tc
        del m
    sep_time, sep_peak = time.time() - t0, torch.cuda.max_memory_allocated()

    # arm 2: cross-wired
    torch.cuda.reset_peak_memory_stats()
    trunk = load(root / t["trunk"])
    n_layer = trunk.config.num_hidden_layers
    cw = {c: [] for c in cells}
    t0 = time.time()
    for p in prompts:
        ids = tok(p, return_tensors="pt").input_ids.to(dev)
        pos = torch.arange(ids.shape[1], device=dev)[None]
        h0 = trunk.model.embed_tokens(ids)
        pe = trunk.model.rotary_emb(h0, pos)
        shared = run_layers(trunk, h0, pos, pe, 0, split)  # once for the whole tissue
        for c in cells:
            hooks = adapter_hooks(trunk, adroot / c, dev)
            h = run_layers(trunk, shared, pos, pe, split, n_layer)
            cw[c].append(trunk.lm_head(trunk.model.norm(h))[0, -1].float().cpu())
            for hk in hooks:
                hk.remove()
    cw_time, cw_peak = time.time() - t0, torch.cuda.max_memory_allocated()
    ad_bytes = sum((adroot / c / "adapter_model.safetensors").stat().st_size for c in cells)

    ntok = sum(len(tok(p).input_ids) for p in prompts)
    cfg = trunk.config
    out = {
        "tissue": a.tissue, "split_layer": split, "n_layer": n_layer, "cells": cells, "n_prompts": len(prompts),
        "separate": {"weight_bytes": sep_bytes, "peak_gpu_bytes": sep_peak, "seconds": sep_time,
                     "compute_seconds": sep_compute, "macs": ntok * macs_per_token(cfg, n_layer) * len(cells)},
        "cross_wired": {"weight_bytes": mbytes(trunk) + ad_bytes, "peak_gpu_bytes": cw_peak, "seconds": cw_time,
                        "macs": ntok * (macs_per_token(cfg, split) + macs_per_token(cfg, n_layer - split) * len(cells))},
        "quality": {},
        "note": "macs exclude adapter products and attention scores. 'seconds' for separate includes swapping models in, as a device that cannot hold them all must; 'compute_seconds' leaves that out. Both are reported.",
    }
    for c in cells:
        agree = sum(int(x.argmax() == y.argmax()) for x, y in zip(sep[c], cw[c])) / len(prompts)
        q = {"top1_agreement_with_separate": agree}
        if items:
            q["accuracy_separate"] = sum(answer(x, tok) == d["base"] for x, d in zip(sep[c], items)) / len(items)
            q["accuracy_cross_wired"] = sum(answer(y, tok) == d["base"] for y, d in zip(cw[c], items)) / len(items)
        out["quality"][c] = q
    for k in ("weight_bytes", "macs", "seconds"):
        out[f"saving_{k}"] = 1 - out["cross_wired"][k] / out["separate"][k]
    out["saving_compute_seconds"] = 1 - cw_time / sep_compute
    pathlib.Path(a.out).write_text(json.dumps(out, indent=2))
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
