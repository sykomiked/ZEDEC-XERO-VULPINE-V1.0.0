#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Speech-out measurement (T26, bench/results/2026-10-12-outetts/REPORT.md).

For each sentence: speak it with zt_tts (the integer engine) and, when given, with an
OuteTTS-capable llama-tts (llama.cpp before the tool became model-agnostic, e.g. tag
b6000); transcribe every WAV with whisper.cpp after resampling 24 kHz -> 16 kHz (a
polyphase windowed-sinc step here, test-only, not engine code); report the word error
rate against the normalised input text. With llama-tts it also decodes llama.cpp's own
code ids with zt_tts --codes-in and compares the PCM (same codes, two decoders), and
runs ref_wavtokenizer.py (float64) on the ZXV codes.

usage: tts_roundtrip.py --zt-tts BIN --tts TTS.gguf --dec DEC.gguf --whisper-cli BIN
                        --whisper-model M [--llama-tts BIN] [--threads 2] [--seed 42]
                        [--top-k 4] [--out DIR] [--json FILE] [--ref]
"""
import argparse, json, os, re, subprocess, sys, time
import numpy as np

SENTENCES = [
    "Hello world, it is 3 pm.",
    "The quick brown fox jumps over the lazy dog.",
    "I bought 12 apples and 7 oranges for 5.50 dollars.",
    "Please call me back at 9, or send a short message!",
    "Is the meeting on Monday at 10:30, or on Tuesday?",
]

ONES = ["zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
        "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen",
        "eighteen", "nineteen"]
TENS = ["", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"]


def below1000(n):
    s = ""
    if n >= 100: s += ONES[n // 100] + " hundred "; n %= 100
    if n >= 20: s += TENS[n // 10] + ("-" + ONES[n % 10] if n % 10 else "")
    elif n > 0: s += ONES[n]
    return s


def num_words(m):
    """llama.cpp's number_to_words (the text side of the WER, and whisper's digits)."""
    t = m.group(0); ip, _, fp = t.partition(".")
    n = int(ip)
    if n > 2147483647: return " "
    s = "zero" if n == 0 else ""
    for sc, nm in ((10 ** 9, " billion "), (10 ** 6, " million "), (1000, " thousand ")):
        if n >= sc: s += below1000(n // sc) + nm; n %= sc
    if n > 0: s += below1000(n)
    if "." in t: s += " point" + "".join(" " + ONES[int(d)] for d in fp)
    return s


def words(text):
    # whisper writes "3pm", "3 p.m.", "$5.50": split digits from letters, a.m./p.m. as one
    # word, "$x.yz" as x point y z dollars is left to the number rule
    t = re.sub(r"(\d)([A-Za-z])", r"\1 \2", text)
    t = re.sub(r"\b([apAP])\.\s?[mM]\.?", lambda m: m.group(1).lower() + "m", t)
    t = re.sub(r"\d+(\.\d+)?", num_words, t).lower()
    t = re.sub(r"[-_/,.\\]", " ", t)
    t = re.sub(r"[^a-z\s]", "", t)
    return t.split()


def wer(ref, hyp):
    d = list(range(len(hyp) + 1))
    for i, r in enumerate(ref, 1):
        prev, d[0] = d[0], i
        for j, h in enumerate(hyp, 1):
            prev, d[j] = d[j], min(d[j] + 1, d[j - 1] + 1, prev + (r != h))
    return d[len(hyp)] / max(len(ref), 1)


def read_wav(path):
    b = open(path, "rb").read(); i = b.index(b"data")
    return np.frombuffer(b[i + 8:], dtype="<i2").astype(np.float64)


def write_wav(path, x, rate):
    d = np.clip(np.round(x), -32768, 32767).astype("<i2").tobytes()
    h = b"RIFF" + (36 + len(d)).to_bytes(4, "little") + b"WAVEfmt " + (16).to_bytes(4, "little")
    h += (1).to_bytes(2, "little") + (1).to_bytes(2, "little") + rate.to_bytes(4, "little")
    h += (2 * rate).to_bytes(4, "little") + (2).to_bytes(2, "little") + (16).to_bytes(2, "little")
    open(path, "wb").write(h + b"data" + len(d).to_bytes(4, "little") + d)


def resample_24k_16k(x):
    """Up by 2, Kaiser-windowed sinc low-pass at 7.6 kHz (of 48 kHz), down by 3."""
    up = np.zeros(2 * len(x)); up[::2] = x
    n = np.arange(-96, 97)
    fc = 7600 / 48000
    h = 2 * fc * np.sinc(2 * fc * n) * np.kaiser(len(n), 8.0)
    h *= 2 / h.sum()
    return np.convolve(up, h, mode="same")[::3]


def transcribe(a, wav16):
    out = subprocess.run([a.whisper_cli, "-m", a.whisper_model, "-f", wav16, "-nt", "-np", "-t",
                          str(a.threads), "-l", "en"], capture_output=True, text=True).stdout
    return " ".join(l.strip() for l in out.splitlines() if l.strip())


def kv(out):
    return dict(l.split("=", 1) for l in out.splitlines() if "=" in l)


def snr(got, ref, skip=6000):
    n = min(len(got), len(ref)); g, r = got[skip:n], ref[skip:n]; e = g - r
    return dict(snr_db=float(10 * np.log10((r * r).sum() / max((e * e).sum(), 1e-30))),
                max_abs=float(np.abs(e).max()), rms=float(np.sqrt((e * e).mean())))


def main():
    ap = argparse.ArgumentParser()
    for k in ("zt-tts", "tts", "dec", "whisper-cli", "whisper-model"): ap.add_argument("--" + k, required=True)
    ap.add_argument("--llama-tts"); ap.add_argument("--threads", type=int, default=2)
    ap.add_argument("--seed", type=int, default=42); ap.add_argument("--top-k", type=int, default=4)
    ap.add_argument("--out", default="/tmp/zxv_tts"); ap.add_argument("--json")
    ap.add_argument("--ref", action="store_true", help="run ref_wavtokenizer.py (float64) on ZXV codes")
    ap.add_argument("--sentences", type=int, default=len(SENTENCES))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rows = []
    here = os.path.dirname(os.path.abspath(__file__))
    for i, s in enumerate(SENTENCES[:a.sentences]):
        ref_words = words(s)
        row = {"text": s, "ref_words": " ".join(ref_words)}
        base = os.path.join(a.out, "s%d_k%d" % (i, a.top_k))
        t0 = time.time()
        out = subprocess.run([a.zt_tts, a.tts, a.dec, s, base + "_zxv.wav", "--seed", str(a.seed),
                              "--top-k", str(a.top_k), "--threads", str(a.threads), "--codes-out",
                              base + "_zxv.codes", "--spec-out", base + "_zxv.spec"],
                             capture_output=True, text=True, check=True).stdout
        z = kv(out); z["wall_s"] = time.time() - t0
        x16 = base + "_zxv16.wav"; write_wav(x16, resample_24k_16k(read_wav(base + "_zxv.wav")), 16000)
        hyp = transcribe(a, x16)
        z["transcript"] = hyp; z["wer"] = wer(ref_words, words(hyp))
        row["zxv"] = z
        if a.ref:
            r = subprocess.run([sys.executable, os.path.join(here, "compare", "ref_wavtokenizer.py"), a.dec,
                                base + "_zxv.codes", base + "_zxv.wav", "--spec", base + "_zxv.spec"],
                               capture_output=True, text=True, check=True).stdout
            row["zxv_vs_f64"] = json.loads(r)
        if a.llama_tts:
            t0 = time.time()
            p = subprocess.run([a.llama_tts, "-m", a.tts, "-mv", a.dec, "-p", s, "-o", base + "_llama.wav",
                                "-t", str(a.threads), "--seed", str(a.seed), "--top-k", str(a.top_k)],
                               capture_output=True, text=True)
            log = p.stdout + p.stderr
            l = {"wall_s": time.time() - t0}
            m = re.search(r"prompt eval time =\s*([\d.]+) ms /\s*(\d+) tokens", log)
            if m: l["prefill_s"] = float(m.group(1)) / 1000; l["n_prompt"] = int(m.group(2))
            m = re.search(r"\n[^\n]*\beval time =\s*([\d.]+) ms /\s*(\d+) runs", log)
            if m: l["gen_s"] = float(m.group(1)) / 1000; l["n_gen"] = int(m.group(2))
            for k, pat in (("vocoder_s", r"time for vocoder:\s*([\d.]+) ms"),
                           ("spectral_s", r"time for spectral ops:\s*([\d.]+) ms")):
                m = re.search(pat, log)
                if m: l[k] = float(m.group(1)) / 1000
            m = re.search(r"codes audio: '([^']*)'", log)
            codes = [int(c) for c in re.findall(r"<\|(\d+)\|>", m.group(1))] if m else []
            l["n_codes"] = len(codes)
            l16 = base + "_llama16.wav"
            write_wav(l16, resample_24k_16k(read_wav(base + "_llama.wav")), 16000)
            hyp = transcribe(a, l16)
            l["transcript"] = hyp; l["wer"] = wer(ref_words, words(hyp))
            # the same codes through the integer decoder
            if codes and max(codes) < 4096:
                open(base + "_llama.codes", "w").write(" ".join(map(str, codes)) + "\n")
                subprocess.run([a.zt_tts, "-", a.dec, "-", base + "_llamacodes_zxv.wav", "--codes-in",
                                base + "_llama.codes", "--threads", str(a.threads)],
                               capture_output=True, check=True)
                l["same_codes_zxv_vs_llama"] = snr(read_wav(base + "_llamacodes_zxv.wav"),
                                                   read_wav(base + "_llama.wav"))
            row["llama"] = l
        rows.append(row)
        print(json.dumps(row), flush=True)
    summ = {"zxv_mean_wer": float(np.mean([r["zxv"]["wer"] for r in rows]))}
    if a.llama_tts: summ["llama_mean_wer"] = float(np.mean([r["llama"]["wer"] for r in rows]))
    print(json.dumps(summ))
    if a.json: json.dump({"rows": rows, "summary": summ, "top_k": a.top_k, "seed": a.seed},
                         open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()
