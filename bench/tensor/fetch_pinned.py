#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Download model files pinned by SHA-256 into ~/.cache/zxv/models (never into
the repository). A file whose size or SHA-256 differs from the pin is deleted.

usage: fetch_pinned.py NAME [NAME ...]     fetch a set (see --list)
       fetch_pinned.py --list              print the sets and their licences
Prints one line per file: PATH. Exit 0 ok, 1 mismatch or download failure.
"""
import hashlib, os, sys, urllib.request

CACHE = os.path.expanduser(os.environ.get("ZXV_MODELS", "~/.cache/zxv/models"))

# name: (licence, [(repo, file, sha256, size)])
SETS = {
    "smolvlm": ("Apache-2.0 (HuggingFaceTB/SmolVLM-256M-Instruct; GGUF by ggml-org)", [
        ("ggml-org/SmolVLM-256M-Instruct-GGUF", "SmolVLM-256M-Instruct-Q8_0.gguf",
         "2a31195d3769c0b0fd0a4906201666108834848db768af11de1d2cef7cd35e65", 175054528),
        ("ggml-org/SmolVLM-256M-Instruct-GGUF", "mmproj-SmolVLM-256M-Instruct-Q8_0.gguf",
         "7e943f7c53f0382a6fc41b6ee0c2def63ba4fded9ab8ed039cc9e2ab905e0edd", 103769856),
    ]),
    # OuteTTS-0.3-500M is CC-BY-SA-4.0: commercial use allowed with attribution
    # ("OuteTTS-0.3-500M by OuteAI, CC-BY-SA-4.0"); share-alike applies to the
    # weights and adaptations of them only, our code stays Apache-2.0. NOT the
    # 0.3-1B model, which is CC-BY-NC-SA-4.0 (non-commercial). ggml-org/WavTokenizer
    # is a GGUF of novateur/WavTokenizer-large-speech-75token (MIT).
    "outetts": ("CC-BY-SA-4.0 (OuteAI/OuteTTS-0.3-500M: attribution required, share-alike "
                "on the weights only; not the NC-licensed 0.3-1B) + MIT (WavTokenizer decoder: "
                "novateur/WavTokenizer-large-speech-75token, GGUF by ggml-org)", [
        ("OuteAI/OuteTTS-0.3-500M-GGUF", "OuteTTS-0.3-500M-Q8_0.gguf",
         "e6cb54453b2de1eaee85bd46b67de9c8cdd3c2dfe117cc3e3da794ddc64dad85", 536666880),
        ("ggml-org/WavTokenizer", "WavTokenizer-Large-75-F16.gguf",
         "2356baa8631cc2995ea3465196a017a2733600d849a91180c0f97fa7fb375bbe", 130186688),
    ]),
    "whisper-base": ("MIT (openai/whisper base, ggml by ggerganov; the speech-out WER check)", [
        ("ggerganov/whisper.cpp", "ggml-base.bin",
         "60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe", 147951465),
    ]),
    "smolvlm-f16": ("Apache-2.0", [
        ("ggml-org/SmolVLM-256M-Instruct-GGUF", "mmproj-SmolVLM-256M-Instruct-f16.gguf",
         "0802360aca1748f112ea510b8ff277c65b1361c8ef30ed89b83c9c7a60d08e96", 190031616),
    ]),
}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def fetch(repo, name, sha, size):
    d = os.path.join(CACHE, repo.replace("/", "_"))
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, name)
    if not (os.path.exists(path) and os.path.getsize(path) == size and sha256_file(path) == sha):
        url = f"https://huggingface.co/{repo}/resolve/main/{name}"
        print(f"downloading {url}", file=sys.stderr)
        urllib.request.urlretrieve(url, path + ".part")
        os.replace(path + ".part", path)
        got = sha256_file(path)
        if os.path.getsize(path) != size or got != sha:
            os.remove(path)
            raise SystemExit(f"{name}: sha256 {got}, expected {sha}. Deleted.")
    return path


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__.strip())
        return 2
    if argv[0] == "--list":
        for k, (lic, files) in SETS.items():
            print(f"{k}\t{lic}\t" + " ".join(f[1] for f in files))
        return 0
    for k in argv:
        if k not in SETS:
            raise SystemExit(f"unknown set {k}; see --list")
        for f in SETS[k][1]:
            print(fetch(*f))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
