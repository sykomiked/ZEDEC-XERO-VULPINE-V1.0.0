#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""S5/S6: make a cell four-valued natively.

probe   (S5) Run the cell on make_tfbn_data.py prompts, take the last
        token's final hidden state (after the output norm: what
        zt_model_hidden returns), and write it in the engine's Q16 layout
        with the mu and lambda labels. A float reference fit is reported,
        with its 4x4 confusion on the test split (TRUE/FALSE/BOTH/NEITHER).
        These are the float model's hidden states; the head that ships is
        refitted by zt_evh_fit on the engine's own (quantised) hidden states
        (engine task E1 in README.md), so it matches what the device sees.

adapter (S6) Train a LoRA on the cell (or on its trunk, for a tissue cell,
        only on layers at or above the tissue's split layer so the shared
        lower pass stays shared) to answer TRUE, FALSE, BOTH or NEITHER,
        then run probe on the adapted model. The adapter is written outside
        the repository; weights never go into git.

    python3 train_tfbn.py probe   --model /srv/tiamat/models/Spinewyrm/src --data tfbn.jsonl --out /srv/tiamat/evh/Spinewyrm
    python3 train_tfbn.py adapter --model ... --data tfbn.jsonl --out ... [--split-layer 14 --rank 32]

Outputs (probe):
    evh_data.bin   "ZEVD" u32 n_samples u32 n_embd, then per sample n_embd
                   int32 Q16 values, then u8 y_mu[n], u8 y_la[n]
    report.json    float reference fit: accuracy and confusion per base
"""
import argparse
import json
import pathlib
import struct
import sys

import torch
from transformers import AutoModelForCausalLM, AutoTokenizer

BASES = ["FALSE", "NEITHER", "BOTH", "TRUE"]  # zt_evh codes 0..3


def para_base(mu, la):
    cert, contr = mu - la, mu + la - 1
    if abs(cert) > abs(contr):
        return 3 if cert > 0 else 0
    return 2 if contr > 0 else 1


def rows(path, split=None):
    for l in open(path):
        d = json.loads(l)
        if split is None or d["split"] == split:
            yield d


@torch.no_grad()
def hidden(model, tok, prompts, dev, bs=16):
    out = []
    tok.padding_side = "left"
    if tok.pad_token is None:
        tok.pad_token = tok.eos_token
    for i in range(0, len(prompts), bs):
        enc = tok(prompts[i:i + bs], return_tensors="pt", padding=True).to(dev)
        h = model(**enc, output_hidden_states=True).hidden_states[-1][:, -1, :]
        out.append(h.float().cpu())
    return torch.cat(out)


def fit(h, y, steps=400, lr=0.05, wd=1e-4):
    w = torch.zeros(h.shape[1], requires_grad=True)
    b = torch.zeros(1, requires_grad=True)
    opt = torch.optim.Adam([w, b], lr=lr, weight_decay=wd)
    yt = torch.tensor(y, dtype=torch.float32)
    for _ in range(steps):
        opt.zero_grad()
        loss = torch.nn.functional.binary_cross_entropy_with_logits(h @ w + b, yt)
        loss.backward()
        opt.step()
    return w.detach(), b.detach()


def probe(a, model=None, tok=None):
    dev = a.device
    if model is None:
        model = AutoModelForCausalLM.from_pretrained(a.model, torch_dtype=torch.bfloat16).to(dev).eval()
        tok = AutoTokenizer.from_pretrained(a.model)
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    tr, te = list(rows(a.data, "train"))[: a.max_train], list(rows(a.data, "test"))
    htr = hidden(model, tok, [d["prompt"] for d in tr], dev)
    hte = hidden(model, tok, [d["prompt"] for d in te], dev)
    with open(out / "evh_data.bin", "wb") as f:
        f.write(b"ZEVD" + struct.pack("<II", htr.shape[0], htr.shape[1]))
        q = (htr * 65536).round().clamp(-(2**31), 2**31 - 1).to(torch.int64)
        f.write(q.to(torch.int32).numpy().tobytes())
        f.write(bytes(d["mu"] for d in tr) + bytes(d["lambda"] for d in tr))
    mean, std = htr.mean(0), htr.std(0) + 1e-6
    ztr, zte = (htr - mean) / std, (hte - mean) / std
    wm, bm = fit(ztr, [d["mu"] for d in tr])
    wl, bl = fit(ztr, [d["lambda"] for d in tr])
    def score(items, z):
        mu, la = torch.sigmoid(z @ wm + bm), torch.sigmoid(z @ wl + bl)
        conf = [[0] * 4 for _ in range(4)]
        for d, m, l in zip(items, mu.tolist(), la.tolist()):
            conf[BASES.index(d["base"])][para_base(m, l)] += 1
        acc = sum(conf[i][i] for i in range(4)) / max(1, len(items))
        # the costly error: claiming TRUE or FALSE when the sources conflict or are silent
        fc = sum(conf[t][p] for t in (1, 2) for p in (0, 3)) / max(1, sum(sum(conf[t]) for t in (1, 2)))
        return {"n": len(items), "accuracy": acc, "false_certainty_rate": fc,
                "confusion_rows_truth_cols_pred": BASES, "confusion": conf}

    held = score(te, zte)
    rep = {"model": a.model, "n_train": len(tr), "n_test": len(te), "accuracy": held["accuracy"],
           "false_certainty_rate": held["false_certainty_rate"],
           "confusion_rows_truth_cols_pred": BASES, "confusion": held["confusion"]}
    if a.extra_test:  # T1's hand-written set (tools/tiamat/bench/make_t1_set.py)
        ex = list(rows(a.extra_test))
        hx = hidden(model, tok, [d["prompt"] for d in ex], dev)
        rep["extra_test"] = dict(score(ex, (hx - mean) / std), file=a.extra_test)
    (out / "report.json").write_text(json.dumps(rep, indent=2))
    print(json.dumps(rep, indent=2))


def adapter(a):
    from peft import LoraConfig, get_peft_model
    dev = a.device
    tok = AutoTokenizer.from_pretrained(a.model)
    model = AutoModelForCausalLM.from_pretrained(a.model, torch_dtype=torch.bfloat16).to(dev)
    n_layer = model.config.num_hidden_layers
    cfg = LoraConfig(r=a.rank, lora_alpha=a.rank, lora_dropout=0.05, task_type="CAUSAL_LM",
                     target_modules=["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj"],
                     layers_to_transform=list(range(a.split_layer, n_layer)))
    model = get_peft_model(model, cfg)
    model.print_trainable_parameters()
    opt = torch.optim.AdamW([p for p in model.parameters() if p.requires_grad], lr=a.lr)
    data = list(rows(a.data, "train"))[: a.max_train]
    model.train()
    for epoch in range(a.epochs):
        for i in range(0, len(data), a.bs):
            batch = data[i:i + a.bs]
            texts = [d["prompt"] + " " + d["base"] + (tok.eos_token or "") for d in batch]
            enc = tok(texts, return_tensors="pt", padding=True).to(dev)
            labels = enc.input_ids.clone()
            for j, d in enumerate(batch):  # learn only the answer
                n_prompt = len(tok(d["prompt"]).input_ids)
                labels[j, : n_prompt] = -100
            labels[enc.attention_mask == 0] = -100
            loss = model(**enc, labels=labels).loss
            loss.backward()
            opt.step()
            opt.zero_grad()
            if (i // a.bs) % 50 == 0:
                print(f"epoch {epoch} step {i // a.bs} loss {loss.item():.4f}", flush=True)
    out = pathlib.Path(a.out)
    model.save_pretrained(str(out / "adapter"))
    model.eval()
    probe(a, model, tok)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["probe", "adapter"])
    ap.add_argument("--model", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--max-train", type=int, default=20000)
    ap.add_argument("--extra-test", help="a second test set scored with the same head (T1's hand-written set)")
    ap.add_argument("--split-layer", type=int, default=0)
    ap.add_argument("--rank", type=int, default=32)
    ap.add_argument("--lr", type=float, default=2e-4)
    ap.add_argument("--epochs", type=int, default=1)
    ap.add_argument("--bs", type=int, default=16)
    ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    a = ap.parse_args()
    root = pathlib.Path(__file__).resolve().parents[3]
    if root in pathlib.Path(a.out).resolve().parents:
        sys.exit("--out must be outside the repository (weights never go into git)")
    probe(a) if a.mode == "probe" else adapter(a)


if __name__ == "__main__":
    main()
