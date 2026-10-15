#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Write docs/release/swarm_model_manifest.json from roster.py.

Every admitted model is pinned to the Hugging Face commit we checked and to
the SHA-256 and size of each source file, as the Hub's LFS index reports
them. Where the shipped file is an upstream file (an official or community
GGUF, an ONNX export) it is pinned the same way. Where we build the shipped
file ourselves (an adapter, an F16 conversion) its hash is left null with
"sha256_status": "built on the server"; the server's build writes the real
hash into golden.models, never a guess.

    python3 tools/tiamat/build_manifest.py --fetch          # ask the Hub
    python3 tools/tiamat/build_manifest.py --meta cache.json  # offline
"""
import argparse
import concurrent.futures as cf
import fnmatch
import json
import pathlib
import sys
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import roster  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "docs/release/swarm_model_manifest.json"
HUB = "https://huggingface.co"

# Shipped files taken as published upstream: (repo, file, credit for the packaging).
SHIPPED = {
    roster.T15: [("Qwen/Qwen2.5-1.5B-Instruct-GGUF", "qwen2.5-1.5b-instruct-q4_k_m.gguf", None)],
    roster.T05: [("Qwen/Qwen2.5-0.5B-Instruct-GGUF", "qwen2.5-0.5b-instruct-q4_k_m.gguf", None)],
    roster.TC05: [("Qwen/Qwen2.5-Coder-0.5B-Instruct-GGUF", "qwen2.5-coder-0.5b-instruct-q4_k_m.gguf", None)],
    "Qwen/Qwen2.5-Coder-1.5B-Instruct": [("Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF", "qwen2.5-coder-1.5b-instruct-q4_k_m.gguf", None)],
    roster.TM15: [("mradermacher/Qwen2.5-Math-1.5B-GGUF", "Qwen2.5-Math-1.5B.Q4_K_M.gguf", "GGUF conversion by mradermacher")],
    "Haamipromax/HamAI-Science-1b": [("mradermacher/HamAI-Science-1b-GGUF", "HamAI-Science-1b.Q4_K_M.gguf", "GGUF conversion by mradermacher")],
    "utter-project/EuroLLM-1.7B-Instruct": [("mradermacher/EuroLLM-1.7B-Instruct-GGUF", "EuroLLM-1.7B-Instruct.Q4_K_M.gguf", "GGUF conversion by mradermacher")],
    "HuggingFaceTB/SmolVLM2-500M-Video-Instruct": [
        ("ggml-org/SmolVLM2-500M-Video-Instruct-GGUF", "SmolVLM2-500M-Video-Instruct-Q8_0.gguf", "GGUF conversion by ggml-org"),
        ("ggml-org/SmolVLM2-500M-Video-Instruct-GGUF", "mmproj-SmolVLM2-500M-Video-Instruct-f16.gguf", "GGUF conversion by ggml-org")],
    "llmware/slim-ner-tool": [("llmware/slim-ner-tool", "slim-ner.gguf", None)],
    "onnx-community/Kokoro-82M-v1.0-ONNX": [("onnx-community/Kokoro-82M-v1.0-ONNX", "onnx/model_q8f16.onnx", None)],
    "PaddlePaddle/PP-OCRv6_small_det_onnx": [("PaddlePaddle/PP-OCRv6_small_det_onnx", "inference.onnx", None)],
    "PaddlePaddle/PP-OCRv6_small_rec_onnx": [("PaddlePaddle/PP-OCRv6_small_rec_onnx", "inference.onnx", None)],
}

# Size estimates for files built on the server, marked as estimates in the manifest.
EST_ADAPTER = {"1.5B": 74_000_000, "0.5B": 35_000_000}  # rank-64 all-linear LoRA, F16
# Sources already stored in 16 bits; every other F16 build halves an F32 source.
HALF_ALREADY = {"FrontiersMind/Nandi-Mini-150M-Tool-Calling"}

RUNTIME = {
    "runs now": ["zt (ZXV integer engine)", "llama.cpp"],
    "zt_whisper": ["zt (ZXV integer engine)", "whisper.cpp"],
    "zt_vision": ["zt (ZXV integer engine)", "llama.cpp"],
    "server runtime": ["Transformers", "vLLM"],
}


def fetch(ids):
    def one(i):
        with urllib.request.urlopen(f"{HUB}/api/models/{i}?blobs=true", timeout=60) as r:
            return i, json.load(r)
    with cf.ThreadPoolExecutor(8) as ex:
        return dict(ex.map(one, ids))


def runtime(engine):
    for k, v in RUNTIME.items():
        if engine.startswith(k):
            return v
    return ["Transformers", "ONNX Runtime"] if "ONNX" in engine else ["Transformers"]


def files_of(meta, globs):
    out = []
    for s in meta.get("siblings", []):
        name = s["rfilename"]
        if any(fnmatch.fnmatch(name, g) for g in globs):
            lfs = s.get("lfs") or {}
            out.append({"path": name, "size": s.get("size"), "sha256": lfs.get("sha256")})
    return out


def pinned(meta, repo, path):
    for f in files_of(meta, [path]):
        return {"repo": repo, "revision": meta["sha"], "file": f["path"], "size": f["size"],
                "sha256": f["sha256"],
                "url": f"{HUB}/{repo}/resolve/{meta['sha']}/{f['path']}"}
    raise SystemExit(f"{repo}: {path} not found at {meta.get('sha')}")


def base_chain(meta):
    cd = meta.get("cardData") or {}
    b = cd.get("base_model")
    return [b] if isinstance(b, str) else list(b or [])


def ipfs_node(c):
    """Where the cell lives in the Tiamat bucket tree (tiamat_swarm.json "distribution").
    The CID is filled in by ipfs-dist when the tree is packed; never guessed."""
    branch = "trunks" if c["organ"] == "trunk" else f"organs/{c['organ']}"
    leaf = c["name"].lower()
    layers = ["card"] + (["weights"] if not c["trunk"] else ["trunk_link", "adapter"]) + ["evidence_head"]
    return {"alexandria": f"@swarm.{leaf}", "parent": f"06_tiamat/{branch}",
            "path": f"06_tiamat/{branch}/{leaf}", "layers": layers, "cid": None}


def entry(c, meta, metas):
    src = files_of(meta, c["files"])
    shipped = [pinned(metas[r], r, p) | ({"packaging_credit": cr} if cr else {})
               for r, p, cr in SHIPPED.get(c["id"], [])]
    est = None
    if c["trunk"]:
        est = EST_ADAPTER["1.5B" if "1.5B" in c["trunk"] else "0.5B"]
    elif not shipped and c["ship"].startswith("F16"):
        total = sum(f["size"] or 0 for f in src)
        est = total if c["id"] in HALF_ALREADY else total // 2
    size = sum(s["size"] for s in shipped) if shipped else None
    sha = shipped[0]["sha256"] if len(shipped) == 1 else None
    credits = [c["developer"]] + [s["packaging_credit"] for s in shipped if s.get("packaging_credit")]
    return {
        "model_id": c["id"],
        "dragon_name": c["name"],
        "developer": c["developer"],
        "license": "Apache-2.0",
        "params": c["params"],
        "format": c["ship"],
        "file_size_bytes": size,
        "sha256": sha,
        "sha256_status": "pinned upstream" if shipped else "built on the server",
        "estimated_size_bytes": est,
        "primary_capability": c["cap"],
        "languages": c["langs"],
        "runtime": runtime(c["engine"]),
        "para_synergy": c["para"],
        "iso_seed": c["place"] == "disc",
        "source_url": f"{HUB}/{c['id']}",
        "revision": meta["sha"],
        "placement": c["place"],
        "organ": c["organ"],
        "trunk": c["trunk"],
        "engine": c["engine"],
        "base_models": base_chain(meta),
        "shipped_files": shipped,
        "source_files": src,
        "credits": credits,
        "notes": c["note"],
        "ipfs": ipfs_node(c),
    }


NOTICE = ROOT / "NOTICE"
BEGIN = "TIAMAT MODEL CREDITS"
END = "(end of Tiamat model credits)"


def notice_block(models, excluded):
    rule = "-" * 74
    out = [rule, BEGIN, rule, "",
           "Tiamat Productive Agency AI is built from open models made by the",
           "people and teams below. Each cell carries a Tiamat dragon name; the",
           "upstream model, its creator and its source stay credited here and in",
           "docs/release/swarm_model_manifest.json, whether or not the model's",
           "licence asks for it. Every model listed is Apache-2.0; its licence and",
           "any NOTICE it ships travel with its weights. The dragon names are ours",
           "and do not suggest endorsement by any creator.", ""]
    for m in models:
        out.append(f"{m['dragon_name']}  <-  {m['model_id']}")
        out.append(f"    by {m['developer']}")
        out.append(f"    {m['source_url']} (revision {m['revision'][:12]})")
        if m["base_models"]:
            out.append(f"    built on {', '.join(m['base_models'])}")
        for s in m["shipped_files"]:
            if s.get("packaging_credit"):
                out.append(f"    {s['packaging_credit']}: {s['repo']}")
    out += ["", "Also thanked: creators whose models we checked and could not ship",
            "(see docs/release/SWARM_MODEL_VERIFICATION.md for why):"]
    out += [f"    {e['model_id']}" for e in excluded]
    out += ["", END, ""]
    return "\n".join(out)


def write_notice(models, excluded):
    text = NOTICE.read_text()
    block = notice_block(models, excluded)
    rule = "-" * 74
    if BEGIN in text:
        start = text.index(rule + "\n" + BEGIN)
        stop = text.index(END) + len(END) + 1
        text = text[:start] + block + text[stop:]
    else:
        anchor = rule + "\nCONTRIBUTORS"
        i = text.index(anchor)
        text = text[:i] + block + "\n" + text[i:]
    NOTICE.write_text(text)


GOLDEN = ROOT / "tools/tiamat/golden.models.tiamat"


def write_golden(models):
    """Proposed golden.models lines for the disc set's upstream-pinned GGUFs.
    build_system/golden.models itself is changed only when the user approves
    the disc set; server-built adapters are appended by build_disc_models.sh."""
    out = ["# SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC",
           "# SPDX-License-Identifier: Apache-2.0",
           "# Proposed Tiamat disc set (placement 'disc'), generated by build_manifest.py.",
           "# Same format as build_system/golden.models. Adapters and F16 conversions",
           "# are appended on the server by build_disc_models.sh with their real hashes.",
           "# NAME.gguf  SHA256  BYTES  CID  LICENCE  URL"]
    for m in models:
        if m["placement"] != "disc":
            continue
        for s in m["shipped_files"]:
            if not s["file"].endswith(".gguf"):
                continue
            name = pathlib.Path(s["file"]).name.lower()  # upstream name: disc code may refer to it
            out.append(f"{name} {s['sha256']} {s['size']} - Apache-2.0 {s['url']}")
    GOLDEN.write_text("\n".join(out) + "\n")


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--fetch", action="store_true")
    g.add_argument("--meta")
    ap.add_argument("--out", default=str(OUT))
    a = ap.parse_args()
    need = {c["id"] for c in roster.ADMITTED} | {r for v in SHIPPED.values() for r, _, _ in v}
    metas = fetch(sorted(need)) if a.fetch else json.load(open(a.meta))
    missing = sorted(need - metas.keys())
    if missing:
        raise SystemExit(f"no metadata for {missing}")
    models = [entry(c, metas[c["id"]], metas) for c in roster.ADMITTED]
    disc = [m for m in models if m["placement"] == "disc"]
    gated = [m for m in models if m["placement"] == "disc_gated"]
    doc = {
        "schema": "zxv.tiamat.swarm_model_manifest/1",
        "generated": "2026-10-15",
        "brand": "Tiamat Productive Agency AI",
        "rules": "Apache-2.0 checkpoints; bases and data without incompatible riders; SafeTensors, GGUF or ONNX only. See docs/release/SWARM_MODEL_VERIFICATION.md.",
        "attribution": "Every cell carries its model id, developer and source URL; every creator is credited in NOTICE whether or not the licence asks for it.",
        "disc_bytes": {
            "disc_pinned": sum(m["file_size_bytes"] or 0 for m in disc),
            "disc_estimated": sum(m["estimated_size_bytes"] or 0 for m in disc),
            "gated_pinned": sum(m["file_size_bytes"] or 0 for m in gated),
            "gated_estimated": sum(m["estimated_size_bytes"] or 0 for m in gated),
            "limit": 10_000_000_000,
        },
        "models": models,
        "excluded": [{"model_id": i, "reason_kind": k, "reason": r, "replacement": rep}
                     for i, k, r, rep in roster.EXCLUDED],
        "considered_and_declined": [{"model_id": i, "reason": r} for i, r in roster.CONSIDERED],
        "data_notes": roster.DATA_NOTES,
    }
    pathlib.Path(a.out).write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n")
    write_notice(models, doc["excluded"])
    write_golden(models)
    print(f"{len(models)} models ({len(disc)} disc, {len(gated)} disc_gated), "
          f"{len(doc['excluded'])} excluded -> {a.out}")
    b = doc["disc_bytes"]
    print(f"disc: {b['disc_pinned']:,} pinned + {b['disc_estimated']:,} estimated; "
          f"gated: {b['gated_pinned']:,} pinned + {b['gated_estimated']:,} estimated")


if __name__ == "__main__":
    main()
