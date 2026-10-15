#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Test extract_adapters.py (S3) and bench_crosswire.py (S8) on tiny random Qwen2 models, CPU only.

Run before J5 (needs torch, transformers and safetensors, as S3 does):

    python3 tools/tiamat/server/test_extract_adapters.py

One tissue holds every kind of cell S3 meets: a fine-tune that is a
low-rank change above layer 2 (ADAPTER), one changed everywhere (FULL),
the same low-rank fine-tune in shards with and without their index, an
upstream LoRA on the tissue's (quantised) base, LoRAs on another base or
with a wrong shape, a GGUF-only cell and an empty one. Each must get its
own result while the others still run, and the report must name the
SHA-256 and size of every adapter file it wrote.
"""
import hashlib
import json
import pathlib
import sys
import tempfile

import torch
from safetensors.torch import save_file
from tokenizers import Tokenizer, models, pre_tokenizers, trainers
from transformers import PreTrainedTokenizerFast, Qwen2Config, Qwen2ForCausalLM

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import extract_adapters as ex  # noqa: E402

FAILS = 0


def check(ok, what):
    global FAILS
    print(("  ok   " if ok else "  FAIL ") + what)
    FAILS += not ok


def tokenizer(d):
    tok = Tokenizer(models.BPE(unk_token="<unk>"))
    tok.pre_tokenizer = pre_tokenizers.ByteLevel(add_prefix_space=False)
    tok.train_from_iterator(ex.DEFAULT_PROMPTS * 4, trainers.BpeTrainer(
        vocab_size=320, special_tokens=["<unk>"], initial_alphabet=pre_tokenizers.ByteLevel.alphabet()))
    PreTrainedTokenizerFast(tokenizer_object=tok, unk_token="<unk>").save_pretrained(str(d))
    return tok.get_vocab_size()


def tiny(vocab):
    torch.manual_seed(7)
    cfg = Qwen2Config(vocab_size=vocab, hidden_size=64, intermediate_size=128, num_hidden_layers=4,
                      num_attention_heads=4, num_key_value_heads=2, max_position_embeddings=256,
                      tie_word_embeddings=False)
    return Qwen2ForCausalLM(cfg).to(torch.float32).eval()


def save(model, d, **kw):
    d.mkdir(parents=True, exist_ok=True)
    model.save_pretrained(str(d), safe_serialization=True, **kw)


@torch.no_grad()
def shifted(trunk, vocab, rank, scale, from_layer, seed):
    """A copy of the trunk with a rank-limited change to every adapter target at or above from_layer."""
    g = torch.Generator().manual_seed(seed)
    m = tiny(vocab)
    m.load_state_dict(trunk.state_dict())
    for name, mod in ex.linears(m).items():
        if ex.layer_of(name) >= from_layer:
            o, i = mod.weight.shape
            b = torch.randn(o, rank, generator=g) * scale
            a = torch.randn(rank, i, generator=g) * scale
            mod.weight += b @ a
    return m


def lora(d, trunk, base, r, alpha, bad_shape=False):
    d.mkdir(parents=True, exist_ok=True)
    g = torch.Generator().manual_seed(11)
    t = {}
    for name, mod in ex.linears(trunk).items():
        if name.split(".")[-1] not in ("q_proj", "v_proj"):
            continue
        o, i = mod.weight.shape
        t[f"base_model.model.{name}.lora_A.weight"] = torch.randn(r, i + (1 if bad_shape else 0), generator=g) * 0.3
        t[f"base_model.model.{name}.lora_B.weight"] = torch.randn(o, r, generator=g) * 0.3
    save_file(t, str(d / "adapter_model.safetensors"))
    (d / "adapter_config.json").write_text(json.dumps({
        "peft_type": "LORA", "r": r, "lora_alpha": alpha, "target_modules": ["q_proj", "v_proj"],
        "bias": "none", "modules_to_save": None, "base_model_name_or_path": base}))
    return t


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    with tempfile.TemporaryDirectory() as tmp:
        root, out = pathlib.Path(tmp) / "models", pathlib.Path(tmp) / "adapters"
        tdir = root / "Trunkwyrm" / "src"
        tdir.mkdir(parents=True)
        vocab = tokenizer(tdir)
        trunk = tiny(vocab)
        save(trunk, tdir)
        save(shifted(trunk, vocab, 2, 0.05, 2, 1), root / "Lowrank" / "src")
        save(shifted(trunk, vocab, 64, 0.5, 0, 2), root / "Everywhere" / "src")
        save(shifted(trunk, vocab, 2, 0.05, 2, 1), root / "Sharded" / "src", max_shard_size="200KB")
        save(shifted(trunk, vocab, 2, 0.05, 2, 1), root / "Noindex" / "src", max_shard_size="200KB")
        (root / "Noindex" / "src" / "model.safetensors.index.json").unlink()
        ups = lora(root / "Upstream" / "src", trunk, "unsloth/tiny-unsloth-bnb-4bit", 4, 8)
        lora(root / "Otherbase" / "src", trunk, "someone/other-model", 4, 8)
        lora(root / "Badshape" / "src", trunk, "test/tiny", 4, 8, bad_shape=True)
        (root / "Quantonly" / "src").mkdir(parents=True)
        (root / "Quantonly" / "src" / "q4.gguf").write_bytes(b"GGUF")
        (root / "Empty" / "src").mkdir(parents=True)
        check(len(list((root / "Sharded" / "src").glob("*.safetensors"))) > 1, "the sharded cell really is in shards")

        cells = ["Everywhere", "Lowrank", "Sharded", "Noindex", "Upstream", "Otherbase", "Badshape",
                 "Quantonly", "Empty"]
        ex.CFG["tissues"]["test/tiny"] = {"trunk": "Trunkwyrm", "cells": cells}
        sys.argv = ["extract_adapters.py", "--models", str(root), "--out", str(out), "--tissue", "test/tiny",
                    "--device", "cpu", "--ranks", "4,16"]
        rc = ex.main()
        rep = json.loads((out / "report_Trunkwyrm.json").read_text())
        c = rep["cells"]
        res = {n: c[n]["result"] for n in cells}
        print(json.dumps(res))
        check(rc == 3, "an ERROR cell makes the run exit 3")
        check(set(c) == set(cells), "every cell is in the report, after the ones that failed")
        check(res["Lowrank"] == "ADAPTER" and c["Lowrank"]["split_layer"] == 2
              and c["Lowrank"]["top1_agree"] >= 0.97 and c["Lowrank"]["kl"] <= 0.05,
              "a low-rank change above layer 2 becomes an adapter split at layer 2 that passes the gate")
        check(res["Everywhere"] == "FULL" and "why" in c["Everywhere"], "a change everywhere ships whole")
        check(res["Sharded"] == "ADAPTER", "a sharded checkpoint with its index loads")
        check(res["Noindex"] == "ERROR" and "index" in c["Noindex"]["why"], "shards without their index are named")
        check(res["Upstream"] == "UPSTREAM_ADAPTER" and c["Upstream"]["trained_on_quantised_base"]
              and 0 < c["Upstream"]["next_token_changed"] <= 1, "an upstream LoRA on the quantised base is kept")
        check(res["Otherbase"] == "ERROR" and "test/tiny" in c["Otherbase"]["why"], "a LoRA on another base is refused")
        check(res["Badshape"] == "ERROR" and "do not fit" in c["Badshape"]["why"], "a LoRA of the wrong shape is refused")
        check(res["Quantonly"] == "FULL" and "GGUF" in c["Quantonly"]["why"], "a GGUF-only cell ships whole")
        check(res["Empty"] == "ERROR" and "no weights" in c["Empty"]["why"], "an empty cell is named")
        for n in ("Lowrank", "Sharded", "Upstream"):
            fs = c[n].get("files", [])
            check(fs and all(sha(out / n / f["path"]) == f["sha256"] and (out / n / f["path"]).stat().st_size == f["size"]
                             for f in fs), f"{n}: the report's SHA-256 and sizes are the files'")
        check(sha(out / "Upstream" / "adapter_model.safetensors") == sha(root / "Upstream" / "src" / "adapter_model.safetensors"),
              "an upstream LoRA is copied byte for byte")
        check(not (out / "Everywhere").exists() and not (out / "Otherbase").exists(),
              "no adapter folder for a FULL or refused cell")
        check(rep["shared_split_layer"] == 2, "the shared split is the lowest adapter split")

        ad, facts = ex.upstream_lora(root / "Upstream" / "src", "test/tiny", ex.linears(trunk))
        name = next(iter(ad))
        key = f"base_model.model.{name}"
        want = (ups[key + ".lora_B.weight"] @ ups[key + ".lora_A.weight"]) * (8 / 4)
        check(torch.allclose(ad[name][0] @ ad[name][1], want, atol=1e-5), "an upstream LoRA is scaled by alpha / r")
        check(facts["rank"] == 4 and facts["modules"] == 8, "its rank and module count are reported")
        # S8 on the same tissue: the extracted adapters cross-wired against running them apart
        import bench_crosswire as bc
        bc.CFG["tissues"]["test/tiny"] = ex.CFG["tissues"]["test/tiny"]
        data = pathlib.Path(tmp) / "tfbn.jsonl"
        data.write_text("".join(json.dumps({"prompt": p, "base": "TRUE"}) + "\n" for p in ex.DEFAULT_PROMPTS))
        bout = pathlib.Path(tmp) / "bench.json"
        sys.argv = ["bench_crosswire.py", "--models", str(root), "--adapters", str(out), "--tissue", "test/tiny",
                    "--data", str(data), "--n", "16", "--out", str(bout), "--device", "cpu"]
        bc.main()
        b = json.loads(bout.read_text())
        check(b["cells"] == ["Lowrank", "Sharded"], "the bench takes the extracted adapters only")
        check(all(q["top1_agreement_with_separate"] >= 0.97 and q["passes"] for q in b["quality"].values())
              and b["gate"] == "cross-wired", "exact low-rank adapters pass the cross-wiring gate")
        check(b["saving_weight_bytes"] > 0 and b["saving_macs"] > 0, "cross-wiring saves weights and multiply-adds")
        ex.CFG["tissues"]["test/none"] = {"trunk": "Trunkwyrm", "cells": ["Everywhere"]}
        bc.CFG["tissues"]["test/none"] = ex.CFG["tissues"]["test/none"]
        (out / "report_Trunkwyrm.json").write_text(json.dumps({"cells": {"Everywhere": {"result": "FULL"}},
                                                              "shared_split_layer": None}))
        sys.argv[sys.argv.index("test/tiny")] = "test/none"
        bc.main()
        b = json.loads(bout.read_text())
        check(b["cells"] == [] and b["gate"] is None, "a tissue with no adapter is reported, not crashed on")
    print(f"[{'PASS' if FAILS == 0 else 'FAIL'}] extract_adapters: {FAILS} failed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
