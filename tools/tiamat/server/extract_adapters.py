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

    python3 extract_adapters.py --models /srv/tiamat/models --out /srv/tiamat/adapters \\
        --tissue Qwen/Qwen2.5-1.5B-Instruct [--prompts prompts.jsonl]

Then convert each PEFT adapter for the engine with llama.cpp:
    python3 convert_lora_to_gguf.py --base <trunk dir> --outtype f16 <adapter dir>
"""
import argparse
import json
import pathlib
import sys

import torch
from safetensors.torch import save_file
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


def load(path, dtype):
    p = pathlib.Path(path)
    src = p / "src"
    gguf = sorted(src.glob("*.gguf"))
    kw = dict(torch_dtype=dtype, low_cpu_mem_usage=True)
    if gguf and not list(src.glob("*.safetensors")):
        return AutoModelForCausalLM.from_pretrained(str(src), gguf_file=gguf[0].name, **kw)
    return AutoModelForCausalLM.from_pretrained(str(src), **kw)


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
    for name in t["cells"]:
        cell = load(root / name, dtype).to(dev).eval()
        cm = linears(cell)
        emb = (cell.get_input_embeddings().weight - trunk.get_input_embeddings().weight).norm().item()
        deltas = {k: (cm[k].weight - tm[k].weight).detach() for k in tm}
        best = None
        for split in range(n_layer // 2, -1, -max(1, n_layer // 8)):
            for rank in map(int, a.ranks.split(",")):
                ad = {k: factor(d, rank) for k, d in deltas.items() if layer_of(k) >= split}
                ag, kl = gate(trunk, cell, ad, tok, prompts, dev)
                print(f"{name} split={split} rank={rank} agree={ag:.4f} kl={kl:.4f}", flush=True)
                if ag >= a.min_agree and kl <= a.max_kl:
                    best = (split, rank, ag, kl, ad)
                    break
            if best:
                break
        entry = {"embedding_delta_norm": emb}
        if not best:
            entry["result"] = "FULL"
        else:
            split, rank, ag, kl, ad = best
            d = pathlib.Path(a.out) / name
            d.mkdir(parents=True, exist_ok=True)
            tensors = {}
            for k, (b, aa) in ad.items():
                key = "base_model.model." + k
                tensors[key + ".lora_A.weight"] = aa.to(torch.float16).cpu()
                tensors[key + ".lora_B.weight"] = b.to(torch.float16).cpu()
            save_file(tensors, str(d / "adapter_model.safetensors"))
            (d / "adapter_config.json").write_text(json.dumps({
                "peft_type": "LORA", "r": rank, "lora_alpha": rank, "target_modules": list(TARGETS),
                "layers_to_transform": list(range(split, n_layer)), "bias": "none",
                "base_model_name_or_path": a.tissue}, indent=2))
            entry |= {"result": "ADAPTER", "split_layer": split, "rank": rank, "top1_agree": ag, "kl": kl}
        report["cells"][name] = entry
        del cell
        torch.cuda.empty_cache() if dev == "cuda" else None
    splits = [c["split_layer"] for c in report["cells"].values() if c["result"] == "ADAPTER"]
    report["shared_split_layer"] = min(splits) if splits else None
    out = pathlib.Path(a.out) / f"report_{t['trunk']}.json"
    out.write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    return 0 if all(c["result"] == "ADAPTER" for c in report["cells"].values()) else 3


if __name__ == "__main__":
    sys.exit(main())
