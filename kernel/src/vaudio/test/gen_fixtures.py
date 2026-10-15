#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""gen_fixtures.py - regenerate kernel/src/vaudio/test/zxva_fixtures.inc.

    python3 kernel/src/vaudio/test/gen_fixtures.py

Fixtures for test_zxva.c:
  ZXVA_CHIME    tools/zxva/examples/chime.zxvas compiled by author.py (every
                layer except the residual)
  ZXVA_TONE     one partial, 997 Hz at exactly -6.02 dBFS, phase 0 at t = 0
  ZXVA_NYQ      partials at 10 kHz, 23.5 kHz and 30 kHz
  ZXVA_RES      a 50 ms, 48 kHz / 16-bit test recording analysed by wav2zxva
                with the lossless residual; ZXVA_RES_PCM is that recording
Needs numpy and a C compiler (wav2zxva renders the residual with the kernel
renderer).
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "zxva"))
import author  # noqa: E402
import wav2zxva  # noqa: E402
import zxvafmt as Z  # noqa: E402


def main():
    parts = []
    chime = author.compile_text(open(os.path.join(ROOT, "tools/zxva/examples/chime.zxvas")).read())
    parts.append(Z.c_array(chime, "ZXVA_CHIME", "tools/zxva/examples/chime.zxvas, %d bytes"
                           % len(chime)))

    w = Z.Writer(1, 100000)
    w.partial([(0, 997.0, 0.5, 0.0), (50000, 997.0, 0.5, 0.0), (100000, 997.0, 0.5, 0.0)])
    tone = w.bytes()
    parts.append(Z.c_array(tone, "ZXVA_TONE", "997 Hz partial, amplitude 0.5, 100 ms"))

    w = Z.Writer(1, 50000)
    for f in (10000.0, 23500.0, 30000.0):
        w.partial([(0, f, 0.25, 0.0), (50000, f, 0.25, 0.0)])
    nyq = w.bytes()
    parts.append(Z.c_array(nyq, "ZXVA_NYQ", "partials at 10, 23.5 and 30 kHz, 50 ms"))

    rate, n = 48000, 2400
    t = np.arange(n) / rate
    x = sum(k ** -1.0 * np.sin(2 * np.pi * 330 * k * t + k) for k in range(1, 9))
    x *= np.minimum(1, t / 0.004) * np.exp(-t * 8)
    x += 0.003 * np.random.default_rng(5).standard_normal(n)
    pcm = np.round(x / np.max(np.abs(x)) * 0.7 * 32767).astype(np.int64)
    import tempfile
    wav = os.path.join(tempfile.mkdtemp(prefix="zxvafx"), "res.wav")
    wav2zxva.write_wav(wav, rate, 16, pcm[:, None])
    wr, _, _ = wav2zxva.analyse(wav, residual=True)
    res = wr.bytes()
    parts.append(Z.c_array(res, "ZXVA_RES", "wav2zxva --residual of ZXVA_RES_PCM, %d bytes"
                           % len(res)))
    lines = ["static const int16_t ZXVA_RES_PCM[%d] = {" % n]
    for i in range(0, n, 12):
        lines.append("    " + ", ".join(str(int(v)) for v in pcm[i:i + 12]) + ",")
    lines.append("};")
    body = "\n".join(p.split("\n", 3)[3] if i else p for i, p in enumerate(parts))
    out = os.path.join(HERE, "zxva_fixtures.inc")
    open(out, "w").write(body + "\n".join(lines) + "\n")
    print("wrote %s (chime %d, tone %d, nyq %d, res %d bytes)"
          % (out, len(chime), len(tone), len(nyq), len(res)))


if __name__ == "__main__":
    main()
