# Speech out on the integer engine: OuteTTS-0.3-500M + WavTokenizer (T26)

2026-10-12. Text to 24 kHz speech with no floating point anywhere in the engine:
the OuteTTS language model writes audio-code tokens (zt_model), and a WavTokenizer
decoder plus an all-integer inverse STFT turn them into 16-bit PCM (zt_tts).

## What was built

| File | What |
|---|---|
| `kernel/src/tensor/zt_tts.{h,c}` | Freestanding (no libc, no malloc, no float, 64-bit division only via `zt_udiv64`). OuteTTS 0.3 text normalisation and prompt with the default speaker; code-token mapping; the `wavtokenizer-dec` graph on `zt_nn` blocks; the integer iSTFT. Header comment "T26 SPEECH OUT" documents the arithmetic, measurements and limits. |
| `kernel/src/tensor/zt_tts_tables.h`, `gen_tts_tables.py` | Q30 sine tables (quarter turn at 1/1024, fine step at 2^-20 turn, the 1280-point twiddles), ln 2, 2^48/2pi, and llama.cpp's default speaker (en_male_1) in the v0.3 form; 60-digit Decimal arithmetic, reproducible. |
| `kernel/src/tensor/test_zt_tts.c`, `gen_tts_fixture.py`, `test_tts_fixture.h` | Offline unit test (no downloads, fixture 885 KB). |
| `bench/tensor/zt_tts.c` (+ `make -C bench zt_tts zt_tts_arm64`) | Hosted CLI: text in, WAV out, timings, code/PCM hashes. |
| `bench/tensor/compare/ref_wavtokenizer.py` | Float64 numpy decoder + iSTFT on the same code ids. |
| `bench/tensor/tts_roundtrip.py` | whisper.cpp round trip (WER) for ZXV and llama.cpp, same-codes comparison. |
| `bench/tensor/fetch_pinned.py` | `outetts` set (the WER check uses the `whisper-cpp-base` set) (SHA-256 + size). |

The pipeline mirrors llama.cpp's OuteTTS path. The current llama.cpp `llama-tts`
no longer supports OuteTTS (it was made model-agnostic in PR #26254), so the
reference is llama.cpp tag **b6000** (`tools/tts/tts.cpp`), built separately for
the comparison; the decoder graph is the same in both versions
(`src/models/wavtokenizer-dec.cpp`).

- **Prompt**: `<|im_start|>\n` + speaker words + normalised text + `<|text_end|>\n` +
  speaker audio codes. Text normalisation = llama.cpp's `process_text` for v0.3
  (numbers to words with its exact rules, lower case, `-_/,.\` to spaces, drop the
  rest, collapse, `<|space|>`). The prompt tokenises (zt_tok, parse_special) to the
  same 869 tokens llama.cpp reports for "Hello world, it is 3 pm.".
- **Sampling**: llama.cpp's OuteTTS chain is top-k 4 then `dist` (no temperature
  sampler is in the chain, so effectively temperature 1). ZXV uses `zt_sample`
  with top-k 4, temperature 1.0: the same distribution, different random numbers.
  Stops at `<|im_end|>`, `<|endoftext|>` or `<|audio_end|>`; max 4096 tokens.
- **Codes**: tokens `<|0|>`..`<|4095|>` (ids 151672..155767) -> 0..4095. llama.cpp
  keeps ids up to 155772, which would pass `<|4096|>`..`<|4099|>` and `<|t_0.00|>`
  to a 4096-entry codebook; ZXV drops them.
- **Decoder** (WavTokenizer-Large-75): codebook (512) -> conv1d k7 (768) -> PosNet
  (ResNet x2, single-head attention over all frames, ResNet x2, GroupNorm; 32 groups)
  -> LayerNorm -> 12 ConvNeXt (dw k7, LN, 768->2304, GELU tanh form as ggml, ->768,
  gamma, residual) -> LayerNorm -> linear to 1282 (641 log-magnitudes + 641 phases).
- **iSTFT** (integer): mag = e^min(x, ln 100) in Q16 (x > 0 as 2^k e^r); cos/sin of
  the phase in Q30 by exact angle reduction (Q16 angle x round(2^48/2pi) = turns in
  64 bits) + tables + one Taylor step; a direct 1280-point inverse DFT in exact 64-bit
  sums, **exactly llama.cpp's `irfft`** (sum over the 641 bins / 641, which weights DC
  and Nyquist half as much as numpy's irfft); periodic Hann; overlap-add at hop 320;
  480-sample trim; division by the Hann^2 envelope; round(32767 x), clipped and
  counted. The first 6000 samples are zeroed as llama.cpp does (`--skip`). Output is
  rounded; llama.cpp truncates toward zero (at most 1 step apart).

## Models (weights never in git)

| File | SHA-256 (= HF LFS oid, x-linked-etag) | Bytes | Licence |
|---|---|---|---|
| `OuteAI/OuteTTS-0.3-500M-GGUF/OuteTTS-0.3-500M-Q8_0.gguf` | `e6cb54453b2de1eaee85bd46b67de9c8cdd3c2dfe117cc3e3da794ddc64dad85` | 536666880 | **CC-BY-SA-4.0** |
| `ggml-org/WavTokenizer/WavTokenizer-Large-75-F16.gguf` | `2356baa8631cc2995ea3465196a017a2733600d849a91180c0f97fa7fb375bbe` | 130186688 | MIT |
| `ggerganov/whisper.cpp/ggml-base.bin` (test only) | `60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe` | 147951465 | MIT |

`python3 bench/tensor/fetch_pinned.py outetts whisper-cpp-base` fetches and checks them.

**Licence, plainly.** OuteTTS-0.3-500M is CC-BY-SA-4.0: commercial use is allowed
**with attribution**, and share-alike applies to the weights and adaptations of the
weights only. Our code stays Apache-2.0. Do **not** substitute OuteTTS-0.3-1B, which
is CC-BY-NC-SA-4.0 (non-commercial). Any product or page that ships or serves speech
from these weights must carry the attribution:

> Speech model: OuteTTS-0.3-500M by OuteAI (https://huggingface.co/OuteAI/OuteTTS-0.3-500M), licensed CC-BY-SA-4.0.

`ggml-org/WavTokenizer` is a GGUF conversion of
`novateur/WavTokenizer-large-speech-75token` (MIT). The default speaker's codes come
from OuteTTS's `en_male_1`, as llama.cpp (MIT) ships them.

## Accuracy against float64 (same codes)

`ref_wavtokenizer.py`: the same GGUF dequantised exactly, all float64, llama.cpp's
graph and iSTFT. PCM is compared after the 6000 zeroed samples; for the SNR the
signal is the float64 PCM. Codes are the ZXV-generated ones (top-k 4, seed 42).

| Sentence | frames | log-mag max / RMS err | phase max err | PCM SNR | PCM max err (steps) | iSTFT alone: SNR / max |
|---|---|---|---|---|---|---|
| Hello world, it is 3 pm. | 162 | 0.0055 / 0.00038 | 0.061 | 59.8 dB | 44 | 85.3 dB / 1 |
| The quick brown fox ... | 296 | 0.0026 / 0.00026 | 0.015 | 70.0 dB | 13 | 87.3 dB / 1 |
| I bought 12 apples ... 5.50 dollars. | 366 | 0.0034 / 0.00030 | 0.024 | 68.9 dB | 16 | 88.0 dB / 1 |
| Please call me back at 9, ... | 300 | 0.0030 / 0.00028 | 0.043 | 70.3 dB | 16 | 86.7 dB / 1 |
| Is the meeting on Monday at 10:30, ... | 279 | 0.0077 / 0.00032 | 0.071 | 67.1 dB | 27 | 87.3 dB / 1 |

("iSTFT alone" = the float64 iSTFT of ZXV's own spectrogram against ZXV's PCM: the
integer iSTFT is within one int16 step everywhere.) The spectrogram's RMS is about
9.9. Zero Q16 saturations and zero PCM clips in every run.

**Same codes, three decoders.** llama.cpp's own codes for each sentence, decoded by
ZXV (`--codes-in`), by llama.cpp and by float64:

| Sentence | ZXV vs float64 | llama.cpp vs float64 | ZXV vs llama.cpp |
|---|---|---|---|
| 1 | 72.6 dB (max 5) | 65.6 dB (max 27) | 64.7 dB |
| 2 | 70.6 dB (max 13) | 62.8 dB (max 57) | 62.4 dB |
| 3 | 65.4 dB (max 52) | 62.7 dB (max 53) | 63.1 dB |
| 4 | 68.1 dB (max 28) | 48.9 dB (max 416) | 49.5 dB |
| 5 | 70.5 dB (max 9) | 64.0 dB (max 38) | 63.2 dB |

The integer decoder is closer to exact arithmetic than llama.cpp's float32/f16
decoder on all five (llama.cpp runs its conv im2col in f16).

**Lean weights.** `ZT_TTS_LOAD_LEAN` / `--lean` keeps one 8-bit block per 32 F16
weights (zt_nn's float residual off): 75 MB of arena instead of 150 MB, and the
decoder about 2x faster, but on sentence 1 the PCM SNR falls to **28.0 dB**
(log-magnitude error up to 0.22, phase up to 1.43 rad). The residual block stays
the default.

## Intelligibility: whisper.cpp round trip

Each WAV is resampled 24 -> 16 kHz (polyphase windowed sinc in the script;
test-only), transcribed by whisper.cpp base (`-l en -nt`), and scored by WER against
the input text. The reference and the transcript go through the same normaliser
(llama.cpp's number words; "$5.50" = "5.50 dollars"; "10:30"/"10.30" = two numbers;
"3pm" = "3 pm"), so whisper's number formatting is not scored. llama.cpp = tag b6000
`llama-tts`, same model files, `--seed 42`, 2 threads.

**Default sampling (top-k 4, seed 42):** mean WER **ZXV 1.8 %, llama.cpp 1.8 %**.

| Sentence | ZXV transcript | WER | llama.cpp transcript | WER |
|---|---|---|---|---|
| Hello world, it is 3 pm. | Hello world, it is 3pm | 0 | Hello World, it is 3 p.m. | 0 |
| The quick brown fox jumps over the lazy dog. | (exact) | 0 | (exact) | 0 |
| I bought 12 apples and 7 oranges for 5.50 dollars. | I bought 12 apples and 7 oranges for $5.50. | 0 | (same) | 0 |
| Please call me back at 9, or send a short message! | Please call me back 9 or send a short message. | 0.09 | Please call me back at 9 a send a short message. | 0.09 |
| Is the meeting on Monday at 10:30, or on Tuesday? | ... at 10.30 or on Tuesday? | 0 | ... at 10 30 or on Tuesday? | 0 |

**Greedy (top-k 1):** mean WER **ZXV 3.6 %, llama.cpp 2.2 %**. ZXV's only error is
whisper writing "at 1030" (scored as "one thousand thirty"); llama.cpp's is "lazy
door". Greedy decoding in float and in integer arithmetic diverges at near-ties, so
the utterances differ (sentence 1: 173 against 323 codes).

Raw results: `roundtrip_topk4.json`, `roundtrip_greedy.json`. Five sentences are a
smoke test of intelligibility, not a statistical WER.

## Speed (this machine)

A 4-core Intel Xeon @ 2.10 GHz (AVX-512 VNNI), **shared with two other workers
running qemu jobs (load average 4 to 7) during every measurement**: the absolute
numbers are pessimistic and noisy. 2 threads unless noted.

| | ZXV (avx512vnni, 2 threads) | llama.cpp b6000 (2 threads) |
|---|---|---|
| Prompt prefill (~875 tokens) | 44 - 51 tok/s (17 - 20 s) | 266 - 376 tok/s (2.3 - 3.3 s) |
| Code generation | 10 - 17 tok/s | 19 - 33 tok/s |
| Decoder + iSTFT, real-time factor (s per s of audio) | **0.48 - 0.74** | 0.14 (decoder) + 0.78 - 0.86 (its O(n^2) float irfft) = 0.9 - 1.0 |

ZXV's whole vocoder runs faster than real time and faster than llama.cpp's (whose
naive irfft dominates); ZXV's language model is 2 - 7x slower than llama.cpp's,
prefill especially. The speaker prefix is the same ~850 tokens for every sentence,
so caching its KV state (zt_model_state_seek) would remove most of the prefill; not
done here. Per kernel set (one run each, "Hello world, it is 3 pm."):

| kernel, threads | prefill tok/s | gen tok/s | decoder RTF |
|---|---|---|---|
| c, 1 / 2 | 14.5 / 26.8 | 6.3 / 11.1 | 1.34 / 0.73 |
| avx2, 1 / 2 | 25.2 / 45.3 | 10.4 / 16.9 | 1.00 / 0.56 |
| avx512, 1 / 2 | 28.1 / 54.1 | 10.8 / 18.6 | 0.85 / 0.48 |
| avx512vnni, 1 / 2 | 29.2 / 48.5 | 10.4 / 16.5 | 0.88 / 0.53 |

Decoder memory: a 150 MB arena (F16 matrices as two 8-bit blocks) and about 37 KB
of work per frame.

## Bit identity

`bit_identity.log`. "Hello world, it is 3 pm.", seed 42, top-k 4:

- x86-64, kernel sets c, avx2, avx512 and avx512vnni, each with 1 and 2 threads: all
  eight give `codes_fnv=19c8427048ddd211` (162 codes) and `pcm_fnv=f9817db845a627ee`.
- aarch64 static build under qemu-aarch64, decoder on those codes (`--codes-in`),
  kernel sets c and neon: `pcm_fnv=f9817db845a627ee`, and the WAV is byte-identical
  to x86's.
- aarch64 under qemu, neon, the full LM path (869-token prefill + 24 generated
  tokens, `--max 24`): `codes_fnv=e52ed7cdeabc1c1d`, `pcm_fnv=063fd91c16b9ec95`,
  the same as x86 with c.
- The unit test pins a golden FNV of the fixture decoder's spectrogram + PCM
  (`0xd33803d39bf0c0ee`); the arm64 build of the test reproduces it.

## Unit test (offline, CI)

`test_zt_tts.c` (33 checks on x86 with -DZT_TEST_SIMD; 29 on arm64): text
normalisation cases (llama.cpp's own example, commas, decimals, INT_MAX and
overflow, bytes >= 0x80, white space), the prompt's structure, code mapping; e^x
(relative error 3.5e-5 for 0 < x <= 10, 0.86 Q16 units for x <= 0); cos/sin over the
whole Q16 range against libm long double (max 2.4 Q30 units); the iSTFT on a random
5-frame Q16 spectrogram against float64 (max 0.516 int16 steps, i.e. rounding only;
84.7 dB) and on a DC-only frame; the tiny decoder (64 codes, 32 channels, F16 plus
one Q8_0 matrix, 7 codes) against float64: spectrogram max error 0.0009
(log-magnitude) / 0.0008 (phase), PCM 70.4 dB; the lean decoder 29.3 dB; every
kernel set, with and without 3 threads, gives the C reference's bits; the golden
hash; refusals (a bad code, a small work area, a wrong architecture). ASan/UBSan
clean. Freestanding compile of `zt_tts.c` for aarch64-none-elf
(-mgeneral-regs-only) and i386-none-elf: only `zt_*` symbols undefined.

## Honest limits

- One checkpoint pair, five short English sentences, one seed per setting.
- The LM is much slower than llama.cpp's (prefill 6 - 8x); the decoder is fine.
- No speaker files, only the built-in en_male_1; no guide tokens; English only (as
  llama.cpp: no romanisation). Number words follow llama.cpp's rules, not English
  grammar ("1,000" -> "one zero"; "10:30" -> "ten thirty" only because ":" is dropped).
- Different random numbers from llama.cpp, so the same seed is a different utterance.
- A direct O(n^2) inverse DFT (0.8 M multiply-adds per frame), not an FFT; the PosNet
  attention is quadratic in the utterance length and holds all frames at once.
- Saturation counts cover matrix products, residual adds and the gamma scale, not
  the norms or the depthwise convolution.
- Timings were taken on a machine shared with two other heavy jobs.
