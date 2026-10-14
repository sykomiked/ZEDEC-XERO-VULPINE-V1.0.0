# Speech in on the integer engine: Whisper base (2026-10-12)

The integer engine now turns speech into text. 16 kHz audio becomes a log-mel
spectrogram, then Whisper's audio encoder and text decoder run on it, and
greedy decoding produces the transcript. Everything is integer arithmetic,
the spectrogram included. Results are bit-identical on every kernel set,
thread count and architecture. On the 7 clips measured, the transcripts are
word-for-word identical to whisper.cpp's.

## Model

Weights are never in git. `bench/tensor/fetch_pinned.py whisper-base`
downloads the Hugging Face files into `~/.cache/zxv/models` and deletes any
file whose SHA-256 differs. `bench/tensor/convert_whisper.py` checks the
pinned SHA-256 of the safetensors again before converting.

| File | Bytes | SHA-256 | Licence |
|---|---|---|---|
| openai/whisper-base `model.safetensors` | 290403936 | `07cadb9f25677c8d50df603e66a98fbd842cce45047139baeb16e6219a1e807b` (the HF LFS oid / x-linked-etag) | Apache-2.0 on Hugging Face (MIT upstream) |
| openai/whisper-base `config.json`, `generation_config.json`, `vocab.json`, `added_tokens.json`, `tokenizer.json` | see `fetch_pinned.py` | pinned in `fetch_pinned.py` | same |
| converted `whisper-base-q8_0.gguf` (our output, not distributed) | 82648160 | `62a712f894ff2722a424cd84b38b782efec00b179c9dcc1c42e92957e156c820` | derived |
| ggerganov/whisper.cpp `ggml-base.bin` (comparison only; set `whisper-cpp-base`) | 147951465 | `60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe` | MIT |

The conversion is deterministic: two runs gave the same SHA-256.

```
python3 bench/tensor/fetch_pinned.py whisper-base
python3 bench/tensor/convert_whisper.py ~/.cache/zxv/models/openai_whisper-base \
    ~/.cache/zxv/models/openai_whisper-base/whisper-base-q8_0.gguf
```

How the converter stores each tensor:
- **Q8_0** (`gguf.quantize`): every matrix whose rows are a multiple of 32
  long. That covers attention, MLP, the token embedding (also the tied output
  matrix) and conv2. conv2 is flattened to rows of 512x3, column `c*3 + j`.
- **conv1**: rows of 80x3 = 240, so it is stored as **F32**. zt_nn keeps it
  in the arena as an 8-bit block plus an 8-bit residual block.
- **F32**: norms, biases, both position tables and the mel filterbank.

The mel filterbank is librosa's slaney mel, 80x201, computed by the converter
with numpy. It matches whisper's own `mel_filters.npz` to 1.3e-9; 118 of the
16080 values differ by one float32 ulp.

The tokenizer is stored as `tokenizer.ggml.*`: GPT-2 byte-alphabet strings,
the specials as CONTROL, and `pre = "whisper"`. The file also carries the
special-token ids, the 99 language codes, and `suppress_tokens` /
`begin_suppress_tokens` from `generation_config.json`.

## What was built

### `kernel/src/tensor/zt_whisper.{h,c}` (T25 SPEECH IN)

Freestanding, integer only, with no libc, malloc, floating point or 64-bit
`/`. The API follows zt_vision:
- config, arena_bytes, and a two-pass load;
- state_bytes and state_init;
- `zt_whisper_mel`;
- `zt_whisper_encode`, which also builds the cross K/V once per utterance;
- `zt_whisper_decode`, with a KV cache, returning the last logits or all of
  them;
- `zt_whisper_detect_lang`;
- `zt_whisper_greedy`, with a per-step callback;
- `zt_whisper_transcribe`.

Text comes from `zt_tok_load` / `zt_tok_decode` on the same file. The header
comment documents the arithmetic and the limits.

**Log-mel.** Steps, per frame:
1. The windowed samples `s * (1 - cos)` are computed exactly in 64 bits.
2. They are shifted down by the fewest bits that keep their absolute sum
   below 2^31. This is a per-frame block exponent.
3. A direct real DFT runs against a Q30 cosine table. The table comes from
   `gen_whisper_tables.py` with 60-digit decimal arithmetic. Sums are exact
   64-bit and stay below 2^61.
4. The power stays below 2^63.
5. The filterbank is applied with about 35 fraction bits, as an exact 96-bit
   sum held in two 64-bit words.
6. log2 is taken to 24 fraction bits by repeated squaring, then multiplied by
   log10(2) in Q32 and clamped at -10.
7. The max-8 floor and (x+4)/4 are applied in Q24, and the result is rounded
   to Q16.

Frames that are all zero are skipped. Frames are independent, so a hosted
`par_rows` splits them across threads.

**Encoder and decoder** use only zt_nn blocks.
- Encoder: conv1 and conv2 as im2col plus a matrix product, GELU (erf), the
  position table, pre-LN blocks, and a final LN.
- Decoder: a dequantised token-embedding row plus the learned position;
  causal self-attention over a Q16 KV cache; cross-attention; the MLP; the
  final LN; and the tied logits.

**Greedy decoding, without timestamps.**
- The prompt is `<|startoftranscript|><|lang|><|transcribe|><|notimestamps|>`.
- At every step, `suppress_tokens` and every special token after
  `<|endoftext|>` (including timestamps) are excluded.
- At the first step, `begin_suppress_tokens` (" " and `<|endoftext|>`) are
  excluded too.
- Ties go to the lower id.
- Decoding stops at `<|endoftext|>`, at 224 tokens, or at the end of the
  text context.

Language detection is the argmax over the language tokens after
`<|startoftranscript|>`.

### Tools and tests

- `bench/tensor/convert_whisper.py`: converts the safetensors to GGUF.
- `bench/tensor/zt_asr.c` (built with `make -C bench zt_asr [ARM64=1]`): the
  command-line driver.

  ```
  zt_asr MODEL.gguf in.wav [--kern c|avx2|avx512|avx512vnni|neon] [--threads N]
         [--lang CODE|auto] [--max-tokens N] [--dump-mel f] [--dump-enc f] [--dump-logits f]
  ```

  It accepts only 16 kHz mono 16-bit PCM WAV; anything else exits with code 6
  and the reason. It prints the transcript, the timings, and FNV-1a hashes of
  the mel, the encoder output and every step's logits. The default language
  is `en`, as in whisper-cli; `auto` detects it.
- `bench/tensor/compare/ref_whisper.py`: a float64 numpy forward pass on the
  same GGUF. Q8_0 is dequantised exactly, the STFT uses numpy's FFT, and GELU
  uses `math.erf`. It reports the gap at each stage.
- `bench/tensor/whisper_eval.py`: runs zt_asr, the float64 reference and
  whisper-cli on each clip, and computes the WER.
- `kernel/src/tensor/test_zt_whisper.c`, generated from
  `gen_whisper_fixture.py` into `test_whisper_fixture.h` (571 KB,
  deterministic).
  - The fixture is a tiny random Whisper: 80 mels with the real filterbank,
    width 32, 2 heads, 2+2 layers. It runs on 1 s of a test signal built from
    an integer recipe, and is checked against float64 values.
  - The test also checks batch against one-token-at-a-time decoding,
    language detection, the greedy chain, a tokenizer round trip, silence
    and full-scale edge cases, and every SIMD kernel set, with and without
    threads, against the C reference.
  - It runs offline. Results: 30/30 on x86-64 (also under ASan/UBSan) and
    26/26 on aarch64 under qemu (NEON + C).

## Accuracy

### Against float64 on the same weights (`ref_whisper.py`)

Each gap is the maximum of |ZXV - f64|. Relative RMS is the RMS of the error
divided by the RMS of the reference.
- **mel** is compared with the float64 log-mel of the same PCM.
- **enc** and **logits** are end to end: the float64 encoder runs on the
  float64 mel, and the float64 decoder is fed the same tokens.

| clip | mel max | enc max | enc rel RMS | logits max | logits rel RMS | argmax agrees | f64 min top-2 gap |
|---|---|---|---|---|---|---|---|
| jfk.wav | 1.06e-05 | 0.116 | 4.68e-04 | 0.015 | 1.20e-04 | 26/26 | 0.326 |
| 1272-128104-0000 | 1.21e-05 | 0.101 | 3.89e-04 | 0.016 | 2.14e-04 | 23/23 | 0.995 |
| 1272-128104-0004 | 1.13e-05 | 0.085 | 3.50e-04 | 0.019 | 2.11e-04 | 88/88 | 0.065 |
| 1272-128104-0009 | 1.30e-05 | 0.092 | 3.93e-04 | 0.056 | 3.28e-04 | 55/55 | 0.121 |
| 1272-135031-0005 | 9.42e-06 | 0.176 | 5.11e-04 | 0.024 | 2.93e-04 | 20/20 | 0.061 |
| 1272-141231-0000 | 1.16e-05 | 0.130 | 5.03e-04 | 0.025 | 3.68e-04 | 13/13 | 0.707 |
| 1272-141231-0020 | 1.01e-05 | 0.092 | 4.29e-04 | 0.018 | 3.88e-04 | 12/12 | 0.014 |

Reference scales: mel RMS is about 0.5 (range -1.5 to 1.5); encoder output
RMS is 1.46 (max 24); logits RMS is 34 (max 73).

- **Mel error** is at the level of Q16 rounding (7.6e-6) plus a little.
- **Encoder error** comes mostly from the Q16 softmax weights. A float64
  simulation that rounds only `e^(s-max)` to 2^-16 reproduces it: rel RMS
  4.1e-4, against ZXV's 4.7e-4 on jfk. With 1500 keys, many weights are near
  2^-11, so each weight's absolute rounding is a sizeable fraction of it.
- **conv1 rounding.** Before the zt_nn change that keeps a residual 8-bit
  block for float matrices (merged from bac21ab), conv1 was rounded to 8 bits
  once, and the encoder gap was 10 times larger on jfk: max 1.06, rel RMS
  4.1e-3, logits max 0.11. conv1 sees the raw mel, so it is the most
  sensitive matrix.
- **Argmax** agrees with float64 at all 237 decoding steps, including one
  where the top-2 gap is only 0.014.

### Against whisper.cpp (greedy, same decoding policy)

whisper.cpp was run as:

```
whisper-cli -m ggml-base.bin -f X.wav -bs 1 -bo 1 -nf -nt -sns -l en -t 2
```

That is greedy decoding with no temperature fallback, no timestamps,
non-speech tokens suppressed, and whisper.cpp's own F16 weights.

**Clips.**
- `~/whisper.cpp/samples/jfk.wav`.
- 6 LibriSpeech dev-clean utterances from
  `hf-internal-testing/librispeech_asr_dummy`, file
  `clean/validation-00000-of-00001.parquet` (SHA-256
  `4e69a06fa5edc90921e5e7e39a7084881f8b3ed9c805c574f4f39c6fde27c603`), rows
  0, 4, 9, 20, 40 and 60.

The FLAC audio was converted with `ffmpeg -ar 16000 -ac 1 -c:a pcm_s16le`,
which is lossless at 16 kHz. Audio is not in git.

| clip | seconds | tokens | same text as whisper.cpp | WER ZXV vs truth | WER whisper.cpp vs truth |
|---|---|---|---|---|---|
| jfk.wav | 11.0 | 25 | yes | 0.000 | 0.000 |
| 1272-128104-0000 | 5.9 | 22 | yes | 0.059 | 0.059 |
| 1272-128104-0004 | 29.4 | 87 | yes | 0.132 | 0.132 |
| 1272-128104-0009 | 18.3 | 54 | yes | 0.093 | 0.093 |
| 1272-135031-0005 | 4.6 | 19 | yes | 0.000 | 0.000 |
| 1272-141231-0000 | 4.7 | 12 | yes | 0.000 | 0.000 |
| 1272-141231-0020 | 4.3 | 11 | yes | 0.000 | 0.000 |

All 7 transcripts are identical to whisper.cpp's, punctuation included, so
the WER between ZXV and whisper.cpp is 0.

Against the LibriSpeech references, both engines get the same WER. Text was
normalised simply: lower case, keeping only letters, digits and apostrophes.
Most of the remaining "errors" are formatting, such as "Mr." for MISTER and
"Michelangelo" for "MICHAEL ANGELO". The rest are whisper-base's own
mishearings ("upgards", "ittles"). The jfk "truth" is the well-known
quotation, not an official transcript.

| clip | WAV SHA-256 | FLAC SHA-256 |
|---|---|---|
| jfk.wav | `59dfb9a4acb36fe2a2affc14bacbee2920ff435cb13cc314a08c13f66ba7860e` | (whisper.cpp sample) |
| 1272-128104-0000 | `12f8eae82f20ce6725b5afbb04805a0d9f4e1b6e3d35598c545882c3fdb878cf` | `4e25e22555cd16e90edb0a3b49fdcf1fe652b2a1250ab643634db33895c75b41` |
| 1272-128104-0004 | `a944593167253cdd5db79934615a8e7eaf2809b64d2faece670121a9bd436e09` | `07244790e9a8300bfcbf12c28ac5230792e75238d03b2ac167a72bf3943c5404` |
| 1272-128104-0009 | `1779068041a9db1db59d30cc5b2b831733424ff4db64a590907ca79e8ef58b01` | `2bfbcdfab6024ccd3bfeb4e883cc7f9cc613235e504bfc068fd34069056513b7` |
| 1272-135031-0005 | `41e0851951cd6319dba37154fe06c02aad3123ee19bda4e8ee7e4a0a6786fef4` | `d3da297f1f6a0f933ce6d778b8dc8fecff4cc7c21616dbd536146e76ebb98fbe` |
| 1272-141231-0000 | `95416a1270dd4a1dfd40ae35b3ce06d9de4e0c2ddd307083227103e9bf529e35` | `adb240047a1e0732c96b029b7dbd7c81a17c853cbd30aec0ee0d9c4e59fe720a` |
| 1272-141231-0020 | `845ba5932af4ea502fa13c228aba5d3d9fefcb2de3329ab5e8d2fb3b80810c50` | `3998b463548c90f6686faf96432cc237c96f0bd784d8124a1a3c851fd4451acf` |

## Speed

The machine has 4 vCPUs, Intel Xeon @ 2.10 GHz with AVX-512 VNNI. Two other
jobs shared it at the time, so timings vary by about ±20%.

Both engines used 2 threads; zt_asr ran with `--kern avx512vnni --threads 2`.
"dec" includes the 4-token prompt.

| clip | zt mel s | zt enc s | zt dec s | zt tok/s | wcpp mel ms | wcpp enc ms | wcpp dec ms |
|---|---|---|---|---|---|---|---|
| jfk.wav | 0.135 | 3.04 | 0.289 | 90 | 35 | 1079 | 311 |
| 1272-128104-0000 | 0.067 | 2.87 | 0.192 | 120 | 18 | 993 | 239 |
| 1272-128104-0004 | 0.315 | 2.84 | 0.666 | 132 | 111 | 1095 | 1169 |
| 1272-128104-0009 | 0.231 | 2.78 | 0.413 | 133 | 60 | 1047 | 637 |
| 1272-135031-0005 | 0.067 | 3.09 | 0.181 | 111 | 19 | 1075 | 235 |
| 1272-141231-0000 | 0.072 | 3.26 | 0.101 | 129 | 13 | 1016 | 140 |
| 1272-141231-0020 | 0.067 | 3.21 | 0.124 | 97 | 17 | 1084 | 139 |

- **Encoder: about 3 times slower than whisper.cpp** (2.8 to 3.3 s against
  1.0 to 1.1 s).
  - A gprof run (avx512vnni, 1 thread, 6.9 s encoder) puts about 60% of the
    time in attention: `att_qk_512` 26%, `att_wv_512` 26%, `att_rows` 9%,
    plus `zt_exp` 4%.
  - The matrix products (`mmb_tile_vnni`) take only 17%.
  - The attention cost comes from the 1500 keys, non-causal: exact 64-bit
    q·k and weighted-V sums, one query row at a time.
  - A tiled, batched integer attention kernel in zt_nn/zt_simd is the clear
    next step.
- **Decoding** is as fast as whisper.cpp here or faster: 7.5 to 11 ms a token
  against 11 to 13 ms.
- **Mel**: 0.07 to 0.3 s against 13 to 111 ms. It uses a direct 400-point
  DFT rather than an FFT, and skips frames that are all zero.

Encoder on jfk.wav, single thread:

| kernel set | encoder |
|---|---|
| C reference | 19.6 s |
| avx2 | 7.3 s |
| avx512 | 5.8 s |
| avx512vnni | 5.9 s |

## Bit identity

These are `zt_asr` FNV-1a 64 hashes on jfk.wav, all taken after the bac21ab
merge.

| build | kernel | threads | mel | encoder | logits (all steps) | transcript |
|---|---|---|---|---|---|---|
| x86-64 | c, avx2, avx512, avx512vnni | 1 and 2 each | `b6b2fce74f0232da` | `fe0a2aad36a64f54` | `29b153ac96cc57e7` | identical |
| aarch64 static, qemu-aarch64 8.2.2 | c | 2 | `b6b2fce74f0232da` | `fe0a2aad36a64f54` | `29b153ac96cc57e7` | identical |
| aarch64 static, qemu-aarch64 8.2.2 | neon | 1 and 2 | `b6b2fce74f0232da` | `fe0a2aad36a64f54` | `29b153ac96cc57e7` | identical |

The same identity held before the merge: all 8 x86 configurations and qemu
NEON with 2 threads gave encoder hash `e3c6632229c8132e`.

Under qemu, the encoder takes 90 s with the C reference and 75 s (2 threads) / 130 s (1 thread) with
NEON. That is the cost of emulation, not the speed of an aarch64 CPU.

The unit test checks the same property on the tiny model, for every kernel
set, with and without threads, on x86-64 and on aarch64.

## Honest limits

- **One 30 s window.**
  - Longer audio is cut, and zt_asr says so.
  - There is no seek or segment loop, no conditioning on previous text, no
    timestamps, no temperature fallback, no beam search, and no no-speech or
    compression-ratio checks.
  - Long-form transcription needs a loop in the caller.
- **Input format.** Only 16 kHz mono int16 WAV is accepted; resampling and
  down-mixing are left to ffmpeg.
- **Not bit-exact with any reference.**
  - Weights are Q8_0, where whisper.cpp uses F16 and transformers uses
    float32.
  - Activation blocks are 16-bit, activations Q16, softmax weights Q16, and
    GELU comes from a table.
  - The measured gaps are in the accuracy section above.
- **Small sample.** Transcripts agree with whisper.cpp on all 7 clips, but
  that is only about 80 s of audio. A clip with a near-tie in the logits
  could decode differently. No WER on a full test set was measured.
- **Model coverage.**
  - Only whisper-base was run. tiny, small, medium and large use the same
    code but are unmeasured.
  - large-v3's 128 mel bins load. Its filterbank must come from the
    converter's librosa slaney filterbank with `n_mels=128`; that is the same
    construction whisper's npz uses, but it was not checked against the npz.
- **Languages.** Only English was tested. Language detection was tested on
  the tiny model and on jfk.
- **Encoder speed.** About 3 times slower than whisper.cpp, limited by
  attention (see Speed).
- **Memory.**
  - The arena (converted conv1, the filterbank, layer tables) is small.
  - The weights stay in the mapped GGUF (83 MB).
  - The state for one utterance is about 90 MB at base size: the mel, the
    encoder output, cross K/V for 6 layers, the KV cache and the activations.

## Reproduce

```
make -C bench zt_asr ARM64=1 OUT=/tmp/zxv_bench
/tmp/zxv_bench/zt_asr ~/.cache/zxv/models/openai_whisper-base/whisper-base-q8_0.gguf ~/whisper.cpp/samples/jfk.wav --threads 2
python3 bench/tensor/whisper_eval.py --zt-asr /tmp/zxv_bench/zt_asr \
    --model ~/.cache/zxv/models/openai_whisper-base/whisper-base-q8_0.gguf --clips clips.json \
    --whisper-cli ~/whisper.cpp/build/bin/whisper-cli \
    --ggml ~/.cache/zxv/models/ggerganov_whisper.cpp/ggml-base.bin --threads 2 --ref --json eval.json
qemu-aarch64 /tmp/zxv_bench/zt_asr_arm64 MODEL.gguf jfk.wav --kern neon --threads 2
```

`clips.json` is a list of `{"id", "wav", "text"}` entries.
