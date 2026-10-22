<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# ZXVA: vector audio

ZXVA is the sound counterpart of ZXVG. A ZXVG image is a list of shapes that
gets drawn at any pixel size. A ZXVA sound is a list of *parametric layers*
that gets rendered to PCM at any sample rate from 8 kHz to 768 kHz, and at
16, 24 or 32 bits. Nothing in the file is tied to a sample grid. Times are in
microseconds, frequencies are in 1/65536 Hz, and levels are on a logarithmic
code scale. The renderer turns those into samples for whatever device is
attached.

| file | role |
|---|---|
| `zxva.h`, `zxva.c` | the reader and renderer. Integer only, freestanding, no allocation |
| `test_zxva.c`, `test/zxva_fixtures.inc`, `test/gen_fixtures.py` | the test suite and its fixtures |
| `tools/zxva/wav2zxva.py` | analysis: WAV to ZXVA (partials, grains, noise, optional lossless residual) |
| `tools/zxva/author.py` | compiles hand-written `.zxvas` text to ZXVA (example: `tools/zxva/examples/chime.zxvas`) |
| `tools/zxva/zxvarender.c` | CLI: ZXVA to WAV at any rate and depth, using the kernel renderer |
| `tools/zxva/zxvafmt.py` | the Python writer shared by the tools |
| `tools/zxva/fidelity.py` | produces the measurements in this document |
| `zxva_analyse.h`, `zxva_analyse.c` | the analyser in C (PCM to ZXVA, with META): integer only, freestanding, no allocation (section 7) |
| `zxva_stft_math.h`, `zxva_stft_kern.h` | the STFT/peak stage, one source shared by the C reference, CUDA and GLSL |
| `zxva_cuda.c`, `zxva_vk.c` + `zxva_vk.comp` | hosted GPU backends of that stage (CUDA via NVRTC, Vulkan compute); bit-exact with the C reference by design |
| `test_zxva_analyse.c`, `test_zxva_cuda.c`, `test_zxva_vk.c`, `test_zxva_cuda_emu.cpp`, `tools/zxva/test_zxva_gpu.sh` | analyser and GPU tests |
| `kernel/src/media/zui_audio.c` | WAV / AIFF / FLAC decoder that feeds the analyser (MP3, AAC, Opus and Vorbis are refused with a reason) |
| `tools/zxva/zxva_bench.c` | corpus benchmark: every file through every backend, JSON lines and a summary |

## 1. Model

A sound is the sum of these layers, each panned to an output channel pair:

* **PARTIAL**: a sinusoid described by breakpoints `(t, f, level, phase)`.
  Between breakpoints, amplitude and frequency are linear. The phase is the
  exact integral of frequency, quadratic in time. In a *locked* track, the
  phase also carries a constant correction per segment, so that it hits the
  stored phase at every breakpoint. This is McAulay-Quatieri phase locking,
  with quadratic phase plus a correction in place of a cubic. The renderer
  evaluates each sample in closed form from its exact time `n / R`, so the
  phase is never accumulated sample by sample.
* **NOISE**: band levels on fixed band edges, one frame every `hop` µs, with
  levels linear between frames. Each band is white noise from a counter-based
  PRNG, passed through a constant-peak 2nd-order band-pass. Its noise gain is
  exactly `b0`, so it is normalised to give the band the stored power
  (`level^2 / 2`) at every output rate.
* **GRAINS**: transients. A *tone* grain is a windowed sinusoid. A *noise*
  grain is 16 seeded sinusoids spread uniformly over `[f - bw/2, f + bw/2]`.
  Both have a raised-cosine attack, an exponential decay (half-life) and a
  raised-cosine release over the last 1/8 of the grain.
* **VOICE**: rate-independent instruments.
  * MODAL: up to 32 partials, each with a ratio, a level and a half-life.
    Sines, bells and marimbas are written this way.
  * FM: a carrier/modulator pair whose index moves linearly over the voice.
  * PLUCK: a Karplus-Strong string, written as its modal equivalent. It has
    48 harmonics with amplitude `|sin(pi k pos)| / k` and half-life
    `h1 * 65536 / (65536 + damp * (k - 1))`. A literal delay-line string would
    sound different at each rate; the modal form does not.

  All three share a piecewise-linear amplitude envelope (up to 32 points).
* **FVOICE**: frame recipes. A recipe declares a native rate and is
  bit-exact at that rate (section 4). At other rates it is converted with
  integer maths: equivalent content, not identical samples.
* **NORM**: a stored peak and target. The whole mix is scaled by
  `target / peak`, so loudness normalisation needs no pre-pass.
* **RESIDUAL**: `source - render`, taken at the source's native rate and
  depth and coded losslessly. Rendering the parametric layers plus the
  residual at that rate and depth reproduces the recording sample-exactly.

### Rendering guarantees (zxva.h A1..A7)

* **Integer only.** There is no float, no libc and no allocation. All
  memory is the caller's `zxva_work_t`: about 1.2 MB at the default caps,
  mostly the 1024 partial cursors and the residual block buffer. Shrink it
  with `-DZXVA_MAX_TRACKS=...` and the other caps.
* **Deterministic.** Every rounding is spelled out, the PRNG is
  counter-based, and right shifts of negative numbers go through an explicit
  floor helper. gcc 13, gcc 11 and clang produce identical hashes; the test
  pins them.
* **Rate independent.** PARTIAL, GRAIN and VOICE samples are closed forms of
  `n / R`. The chime fixture rendered at 96 kHz and at 48 kHz agrees within
  1 LSB (24-bit) on every shared instant. The 997 Hz tone agrees within
  0 LSB (32-bit).
* **Band-limited.** A sinusoid is faded out by its instantaneous frequency
  between `0.45 R` and `0.48 R`, and nothing is synthesised above `0.48 R`.
  Noise bands are clipped at `0.45 R` and their power is scaled by the part
  kept. FM indices are reduced so that the Carson bandwidth stays below
  `0.45 R`. Measured: a 30 kHz partial rendered at 48 kHz leaves
  -207 dBFS at its 18 kHz alias.
* **Streaming.** `zxva_render(w, frame, out, n)` renders any span.
  Rendering in blocks of any size, in order, gives the same bytes as one
  render; the test covers sizes 1, 7, 128, 333, 1000, 4097 and random sizes.
  Closed-form layers and FVOICE are also exact under arbitrary seeks. NOISE
  continues exactly when the next block starts where the last one ended, or
  jumps forward by no more than its 50 ms pre-roll. A backward seek restarts
  the noise filters with that pre-roll, so the noise differs from a
  straight-through render only by a decayed filter transient. A player that
  pulls blocks in order is always exact.
* **Output.** The mix bus is Q40 (full scale `2^40`). It is quantised to
  16, 24 or 32 bits with optional TPDF dither, which is counter-based and so
  also deterministic, then saturated.
* **Fails closed.** `zxva_check` validates the whole file before anything
  renders: every varint, count, range and time, and the residual bitstreams
  down to the last padding bit. The test refuses every truncation of every
  fixture. It also applies 6000 random byte mutations, each of which is
  either refused or renders cleanly under ASan and UBSan.

### Oscillator accuracy (measured by test_zxva)

| measurement | result |
|---|---|
| `zxva_sin_q30` vs a double-precision reference, 400 000 random phases | SNR **182.3 dB**, peak error 1.8e-9 (-175 dB) |
| a rendered 997 Hz partial at -6.02 dBFS, 96 kHz / 32 bit, THD+N against the ideal sine | **-178.4 dB** |
| `2^-1` level code | exact (`zxva_code_amp(5888) == 2^29`) |

The sine is a quarter-wave Taylor series to x^15 (truncation below 7e-12),
evaluated in Q30 with rounding. The phase is Q64 turns (wraps mod 2^64), and
frequencies are Q60 turns per sample. The output side is limited by 32-bit
quantisation (about -186 dB), not by the oscillator.

## 2. What it cannot do (read before trusting a waveform)

* Partials are excellent for tonal, harmonic and sustained sounds, and for
  synthesised interface sounds. They are decent for voice: voiced parts are
  partials, and fricatives go to noise. They are weak for dense mixes, reverb
  tails, crowds and sharp transients. Those become band noise or a few
  grains, which keep the spectral envelope and the timing but not the
  waveform. The fidelity table shows this. Carry the RESIDUAL when the
  waveform matters.
* The noise layer reproduces band power over time, never the waveform. Its
  spectral resolution is the band set: 2nd-order band-passes, so the envelope
  is smoothed between band centres.
* "Infinite fidelity" at high rates means the format adds no aliasing and no
  quantisation of its own. A partial rendered at 192 kHz is a 192 kHz sine.
  It does **not** invent detail the source never had: an analysed 48 kHz
  recording has nothing above 24 kHz at any output rate.
* The RESIDUAL is exact only at its native rate and depth. At other rates it
  is ignored; no resampler is implied.
* **The reference renderer's arithmetic is part of the format.** A residual
  is `source - render` for *this* renderer. If one rounding step changes, an
  old file is off by one LSB somewhere. This happened during development
  and is why `test_zxva` pins render hashes and a residual round trip. Any
  change to `zxva.c` output must bump the format version, and keep the old
  path for old files.
* NOISE and FVOICE noise give identical samples only at identical rates. At
  another rate the band powers match (within 0.25 dB in the test) but the
  samples differ.
* Not implemented: cubic (MQ) phase interpolation, so frequency steps
  slightly at locked breakpoints; stored sample snippets for transients;
  reverb; HRTF; resampling the residual.
* Breakpoint gaps are 100 µs to 4.19 s and a sound lasts at most about
  71 minutes (`dur_us < 2^32`). Frequencies are below 32 768 Hz, and partial
  levels span 144 dB in 0.0235 dB steps (1/256 octave). Level quantisation
  alone limits a partial's waveform SNR to about 62 dB.

## 3. Byte layout (version 1)

`u8` and `u16le` are plain bytes. `uv` is an unsigned LEB128 varint below
2^32 (at most 5 bytes). `uv64` is the same below 2^64 (at most 10 bytes).
`zz` and `zz64` are zigzag-signed (`(n << 1) ^ (n >> 63)`). A *level code*
`L` is 0 for silence, otherwise amplitude `2^((L - 6144) / 256)` with
`L <= 6144` (0 dBFS). Frequencies are Q16 Hz below 2^31. A *placement*
`(ch, pan)` puts a mono layer on channel `ch` (pan 0) or between `ch` and
`ch + 1` with constant-power pan `0..65536`; in a mono file `pan` is
ignored. Anything out of range makes the whole file invalid.

```
file    := "ZXVA" u8 version(=1) u8 flags(=0)
           uv nch (1..8)  uv dur_us (>= 1)  uv seed
           { chunk }  u8 0x00 (END; nothing may follow)
chunk   := u8 tag  body

0x01 PARTIAL  u8 ch  uv pan  u8 locked(0/1)  uv t0_us  uv nbp (2..65535)
              uv f0  uv L0  u16le phase0 (1/65536 turn)
              (nbp-1) x { uv dt (100..2^22 us)  zz df  zz dL  [zz dph] }
                dph (locked only, -32768..32767) is the correction, in
                1/65536 turn, to the phase the renderer predicts at the
                next breakpoint: phi += nat + dph << 48, where
                nat = (((fa+fb)*dt) mod M) * 2^64 / M, M = 2*10^6*65536
                (exact integers; tools/zxva/zxvafmt.py nat_q64 matches)
0x02 NOISE    u8 ch  uv pan  uv t0_us  uv hop (500..2^20 us)  uv nfr (2..2^20)
              u8 nb (1..32)  (nb+1) x uv edge_hz (1..65535, ascending)
              frame 0: nb x uv L;  frames 1..: nb x zz dL
0x03 GRAINS   (at most one chunk) uv count (1..65535), then count x
              { uv dt0 (from the previous grain's start)  u8 kind (0 tone,
                1 noise)  u8 ch  uv pan  uv dur (100..2^20 us)
                uv attack (<= dur)  uv half (0 or <= 2^24 us)  uv f
                uv bw (tone: 0; noise: f - bw/2 >= 0)  uv L  u16le phase }
0x04 VOICE    u8 kind (0 MODAL, 1 FM, 2 PLUCK)  u8 ch  uv pan  uv t0_us
              uv f (Q16 Hz, >= 1)  u8 nenv (2..32)
              nenv x { uv dt (first 0, then 1..2^22 us)  uv L }  (total <= 2^26 us)
              MODAL: u8 np (1..32) np x { uv ratio (Q16, 1..2^24)  uv L
                                           uv half (0 or 4096..2^26 us) }
              FM:    uv ratio (Q16)  uv index0  uv index1 (Q16 radians, <= 16)
              PLUCK: uv half1 (4096..2^26 us)  uv damp (0..65536)  uv pos (1..32768)
0x05 RESIDUAL (at most one, the last chunk) uv rate  u8 bits (16/24/32)
              uv64 nframes (= ceil(dur_us * rate / 10^6))
              ceil(nframes / 4096) blocks, each nch x { uv nbytes  bytes }
              block bitstream, MSB first: 2 bits order (0..3), 6 bits k (0..40),
              then per sample a Rice code of zigzag(e): q one-bits, a zero, k
              bits; q = 32 one-bits escapes to 40 raw bits. e = s - pred,
              pred of order min(order, i) within the block:
              0, s1, 2s1 - s2, 3s1 - 3s2 + s3. Zero padding to the byte;
              nbytes must match exactly; |s| fits int32.
0x06 FVOICE   uv rate (native, 8000..768000)  uv start  uv len (1..2^26 frames)
              u8 kind (0 TONE, 1 NOISE, 2 SWEEP)  u8 ch  zz64 gain0  zz64 gain1
              (Q30, |g| <= 2^40; gain1 to ch+1)  u8 env (0 NONE, 1 ASR,
              2 HALFSINE, 3 AD)  uv envA  uv envB (ASR: attack, release frames;
              AD: attack frames, decay Q30; else 0 0)
              u8 necho (0..6) [uv delay (frames)  uv gain (Q30)  u8 mirror]
              TONE:  u8 np (1..16) np x { uv f_mHz (< 500 * rate)  uv amp (Q14, <= 2^16)
                                          uv attack (frames)  uv decay (Q30) }
              NOISE: uv seed  uv mul  uv add  uv lp_alpha (Q16, 1..65536)
                     uv hp_alpha (Q16, 0 = none)  uv amp (Q14)
              SWEEP: uv f0_mHz  uv glide (Q30, 1..2^31)  uv amp (Q14)
              (start + len + necho * delay) frames must end within dur_us
0x07 NORM     (at most one) uv64 peak (Q40, >= 1)  uv64 target (Q40, <= 2^62)
0x08 META     (at most one, before RESIDUAL; informative, never changes a sample)
              uv len (>= 1), then len bytes: u8 mver (1; a higher version is
              skipped by len)  cid[36] (CIDv1 raw sha2-256 of the ORIGINAL
              file: 01 55 12 20 || SHA-256)  uv64 src_bytes  uv src_rate
              u8 src_bits (<= 64)  u8 src_kind (0 unknown, 1 WAV, 2 AIFF,
              3 FLAC, 4 raw PCM, 5 other)  u8 analyser (0 unknown, 1 kernel
              zxva_analyse, 2 wav2zxva.py, 3 author.py)  zz snr_cdb
              uv band_err_cdb  u8 exact (0 no residual, 1 verified
              sample-exact, 2 unverified); version 1 must fill len exactly
```

### Rendering one partial segment (zxva.c pt_next / pt_span)

For a segment `a -> b` of length `D` µs at rate `R`, the first sample is
`n0 = ceil(ta R / 10^6)`, and `d` is the fraction of a sample from `ta` to
`n0` (Q32). The renderer derives the following in exact 128-bit integer
maths:

* `F = fa/R + dph*10^6/(65536 D R)` turns per sample (Q60);
* `G = (fb - fa)/(D R^2)` turns per sample^2 (Q60), with `G/2` kept with up
  to 16 extra fraction bits;
* `phase(u) = A + B u + (G/2) u^2` (Q64, wrapping), where
  `A = phi_a + F d + G d^2/2` and `B = F + G d`;
* `amp(u) = Aa + Sa u` (Q46).

Each sample `u = n - n0` costs one sine, two products and two adds. No
error accumulates, and the result does not depend on the rendering block.

## 4. FVOICE semantics (bit-exact at the native rate)

These are the exact operations. `x >> s` means `floor(x / 2^s)`, also for
negative `x`, and all products are computed in 128 bits. `test_zxva`
re-implements this text independently and matches the renderer on all
24 000 samples of a three-voice file.

```
sin(p)   = zxva_sin_q30(p)          Q30 sine of a Q64 phase
TONE     inc_k = floor(f_mHz * 2^64 / (1000 Rn)); ph_k(0) = 0; ph_k(i+1) = ph_k(i) + inc_k
         x_k = (sin(ph_k(i)) * amp_k) >> 14
         e_k(i) = (i * 2^30) / A_k for i < A_k; e_k(A_k) = 2^30;
         e_k(i+1) = (e_k(i) * dec_k) >> 30 afterwards (A_k = 0: starts at 2^30)
         v(i) = sum_k (x_k * e_k(i)) >> 30
NOISE    s(-1) = seed; s(i) = s(i-1) * mul + add (mod 2^32)
         x = int32(s(i)) >> 1; lp += ((x - lp) * a_lp) >> 16; w = lp
         if a_hp: hp += ((w - hp) * a_hp) >> 16; w = w - hp
         v(i) = (w * amp) >> 14
SWEEP    inc(0) = floor(f0_mHz * 2^64 / (1000 Rn)); v(i) = (sin(ph(i)) * amp) >> 14
         ph(i+1) = ph(i) + inc(i); inc(i+1) = min((inc(i) * glide) >> 30, 2^63)
env E(i) NONE 2^30 | ASR (i*2^30)/A ramp, then min(.., ((len-i)*2^30)/R) in the
         last R frames | HALFSINE sin(floor(i * 2^63 / len)) | AD like e_k
         v(i) = (v(i) * E(i)) >> 30
stereo   bus[ch]   += (v(i) * G0) >> 20;  bus[ch+1] += (v(i) * G1) >> 20   (Q40 bus)
echo     instance j = 1..necho starts j*delay later with G_c(j) = (G_c * g(j)) >> 30,
         g(0) = 2^30, g(j) = (g(j-1) * gain) >> 30; mirror swaps G0/G1 on odd j
NORM     bus = (bus * floor(target * 2^32 / peak)) >> 32
output   b-bit sample = clamp((bus + 2^(40-b)) >> (41-b)); no dither for recipes
```

At a render rate `R != Rn`:

* frame counts become `floor((N R + Rn/2) / Rn)`;
* per-frame Q30 multipliers `m` (decays, glide) become `m^(Rn/R)`, computed
  by `zxva_pow_q30` from integer log2/exp2;
* one-pole alphas `a` become `1 - (1 - a)^(Rn/R)`;
* frequencies are recomputed from mHz, and tone partials that would alias
  are dropped.

The content is equivalent (the test measures -0.03 dB of power difference
at 96 kHz), but the samples are not identical.

### Mapping the launch-screen synth (zxl_synth) onto ZXVA

| zxl_synth needs | ZXVA |
|---|---|
| 1. Tone voice: sine partials, frequency in mHz, amplitude Q14, linear attack in frames, exponential decay as a Q30 per-frame multiplier | FVOICE TONE: per partial `f_mHz`, `amp` (Q14), `attack` (frames), `decay` (Q30); up to 16 partials |
| 2. Sustained tone (flute or strings envelope, attack and release in frames) | FVOICE TONE with partial decay `2^30` and voice `env = ASR (attack, release)` |
| 3. Noise voice: LCG seed, one-pole low-pass alpha Q16, optional high-pass for clicks, attack + Q30 decay | FVOICE NOISE: `seed`, `mul`, `add` (any LCG), `lp_alpha` (Q16), `hp_alpha` (Q16, 0 = off; HP = input minus a one-pole LP), `env = AD (attack, decay Q30)` |
| 4. Sweep: exponential pitch glide (Q30 per frame) under a half-sine envelope | FVOICE SWEEP: `f0_mHz`, `glide` (Q30, above 2^30 rises), `env = HALFSINE` |
| 5. Stereo L/R gains as int64 Q-gains; echo of delayed copies (delay in frames, gain g^k, optional mirrored pan) | `gain0`/`gain1` are zz64 Q30 (a Q16 gain `g` is `g << 14`, giving the same floor), `necho`, `delay`, `gain` (g, so tap k gets g^k by repeated floor product), `mirror` |
| 6. Per cue: a stored peak for loudness normalisation | NORM chunk: `peak`, `target` (Q40 bus units), applied to the whole file |
| 7. 48 kHz stereo S16, up to 6 cues mixed, pulled in 128-frame blocks | `zxva_open(w, d, n, 48000, 16, 0)` per cue, `zxva_render_acc` each cue's 128 frames onto one int64 bus, then a single `zxva_quantise`. One work per cue, so 6 cues need 6 works (or `zxva_render` when there is one cue) |
| 8. Bit-exact determinism across machines | integer only, explicit floors; FVOICE is checked against an independent re-implementation; hashes are pinned and identical for gcc 11, gcc 13 and clang |

Where a recipe needs something ZXVA does not have, the choice is either a
new FVOICE kind (version-gated) or changing the recipe. Examples are a
different envelope curve, or rounding where ZXVA floors.

## 5. Fidelity of the analysis (tools/zxva/fidelity.py)

| signal | PCM 16/48k | ZXVA parametric | ratio | SNR all layers | SNR tonal layers | band error | residual | ZXVA+residual | same coder on PCM | sample-exact |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|:---:|
| piano-like tone (inharmonic, decaying) | 144000 B | 8976 B | 16:1 | 38.0 dB | 40.4 dB | 1.58 dB | 53868 B | 62843 B | 48680 B | yes |
| vowel-like formant source (vibrato) | 144000 B | 34723 B | 4:1 | 35.8 dB | 38.0 dB | 0.32 dB | 54345 B | 89067 B | 65510 B | yes |
| white-noise burst | 96000 B | 3176 B | 30:1 | -2.7 dB | 0.0 dB | 3.33 dB | 52397 B | 55572 B | 50313 B | yes |
| click train | 96000 B | 3508 B | 27:1 | 11.6 dB | 14.0 dB | 7.99 dB | 49790 B | 53297 B | 25661 B | yes |
| chord (4 notes x 6 harmonics) | 144000 B | 14718 B | 10:1 | 27.6 dB | 30.4 dB | 0.47 dB | 56166 B | 70883 B | 52866 B | yes |

Columns:

* *SNR all layers*: waveform SNR of the parametric render (partials, grains
  and noise) against the source.
* *SNR tonal layers*: partials and grains only. Noise adds power that is
  uncorrelated with the source, so it lowers waveform SNR even when it makes
  the sound closer.
* *band error*: RMS level difference over 10 ms x 24 ERB-band cells within
  60 dB of the loudest cell. This is the meaningful number for noise.
* *residual*: bytes of the lossless residual.
* *same coder on PCM*: the residual coder applied to the source PCM alone,
  which is what a plain lossless coder of this simple kind would need.
* *sample-exact*: whether "ZXVA + residual" at 48 kHz / 16 bit equals the
  source bit for bit.

Measured with `python3 tools/zxva/fidelity.py` (48 kHz mono, 16 bit,
synthetic sources defined in that script). What the numbers say:

* **Tonal material** (piano, vowel, chord) compresses 4:1 to 16:1 at
  28-38 dB waveform SNR, with band errors under 1.6 dB. Most of the
  remaining error is level quantisation (1/256 octave steps cap a single
  partial near 62 dB), breakpoint simplification at 60 dB below the loudest
  partial, and, for the chord, partials closer together than the analysis
  window resolves (C4 and C5 share harmonics exactly; G4 x2 and C4 x3 are
  0.9 Hz apart). They beat inside the window and come out as one partial.
* **Noise** is not waveform-coded. Its SNR of about -3 dB is what any
  independent noise of the right power gives. The meaningful number is the
  3.3 dB band error. The analysis rejects noise peaks as partials if they do
  not stand at least 12 dB above the local spectral median for most of their
  life (noise peaks measure 4 dB median, 10 dB at the 99th percentile). It
  also rejects them if they do not keep phase coherence across a whole
  window. Without these tests the burst took 335 KB of spurious partials.
* **Clicks** become grains: 27:1, about 12 dB waveform SNR, and 8 dB band
  error. A grain is a tone or noise grain with a fixed envelope shape, so
  click waveforms are only approximated. Stored snippets are not
  implemented.
* **ZXVA + residual is lossless** (bit-exact at 48 kHz / 16 bit) for every
  signal. The residual is coded with a simple fixed-predictor Rice coder.
  On the tonal signals it costs about as much as the same coder applied to
  the PCM directly, so the parametric layer does not yet save bits in
  lossless mode. Its value is that the same file renders at any rate and
  depth. On clicks, lossless mode costs about twice the plain coder. A
  better residual coder (LPC, adaptive Rice) is future work.


## 6. Using it

```
python3 tools/zxva/wav2zxva.py in.wav out.zxva --residual
python3 tools/zxva/author.py tools/zxva/examples/chime.zxvas chime.zxva
cc -std=c11 -O2 -Ikernel/src/vaudio tools/zxva/zxvarender.c kernel/src/vaudio/zxva.c -o zxvarender
./zxvarender chime.zxva chime_192k.wav --rate 192000 --bits 24 --dither
python3 kernel/src/vaudio/test/gen_fixtures.py     # regenerate the test fixtures
python3 tools/zxva/fidelity.py /tmp/zxva_fid       # regenerate section 5
```

API (zxva.h): `zxva_check`, `zxva_open(work, data, n, rate, bits, flags)`,
`zxva_frames`, `zxva_render(work, frame, out_i32, n)`,
`zxva_render_acc(work, frame, acc_i64, n)` and `zxva_quantise`. The
primitives are `zxva_sin_q30`, `zxva_code_amp`, `zxva_exp2_q30`,
`zxva_log2_q32`, `zxva_pow_q30`, `zxva_lcg`, `zxva_onepole` and
`zxva_decay`. Flags: `ZXVA_L_PARTIAL|NOISE|GRAIN|VOICE|FVOICE` (0 means all),
`ZXVA_F_DITHER`, `ZXVA_F_RESIDUAL` and `ZXVA_F_NONORM`.

## 7. The kernel analyser and its GPU backends

`zxva_analyse.c` is the analysis of `wav2zxva.py` in C: integer only,
freestanding (no libc symbols on x86_64; on i386 only libgcc's 64-bit
division helpers, as for `zxva.c`), and no allocation. The caller passes a work
buffer of `zxva_analyse_work_size` bytes and an output buffer of
`zxva_analyse_out_bound` bytes. Input is interleaved int32 PCM. The decoder in
`kernel/src/media/zui_audio.c` produces it from WAV, AIFF or FLAC. MP3, AAC,
Opus and Vorbis are refused with a reason, not decoded. Every file it writes
carries META: the CID and size of the original (the original is kept, by
CID; the ZXVA does not replace it), the analyser, and the measured SNR, band
error and exactness of this conversion.

The vector form scales cleanly. It renders at any rate and depth from the same
partials, so there is no resampling step. It does not add detail the source
did not have: content above the source's Nyquist limit stays absent, and noise
in the source is modelled as noise. The numbers below are the measure of how
close it is. With `residual` set, the native-rate render is the original
sample for sample (checked before the file is returned).

**Parity with `wav2zxva.py`** (`test_zxva_analyse`, both files measured by
`zxva_measure`; the test fails if the C result is worse by more than its
margin):

| fixture (16 kHz) | SNR C / Python | band error C / Python | bytes C / Python |
|---|---|---|---|
| piano | 33.82 / 33.84 dB | 1.25 / 1.28 dB | 5467 / 5415 |
| vowel, 24-bit | 34.37 / 34.40 dB | 0.49 / 0.50 dB | 13089 / 12928 |
| noise burst | -2.63 / -2.63 dB | 5.27 / 5.27 dB | 1477 / 1425 |
| clicks | 1.71 / 1.77 dB | 11.95 / 11.95 dB | 1377 / 1323 |
| stereo chord | 35.12 / 37.61 dB | 0.71 / 0.71 dB | 8521 / 8397 |

The stereo gap comes from close harmonics that fall inside one main lobe of
the integer window, which the float analysis separates slightly better.
Noise and clicks have low waveform SNR because they are modelled as noise
bands and grains, not as waveforms. Band error is the measure that matters
for them.

**GPU-first, CPU reference.** The heavy stage (windowed FFT, magnitudes, peak
mask, one frame per workgroup) sits behind `zxva_backend_t`.
`zxva_backend_cpu` is the C reference. `zxva_cuda.c` (CUDA, kernel compiled at
run time by NVRTC, driver and NVRTC loaded with `dlopen`, so it builds with no
CUDA toolkit) and `zxva_vk.c` (Vulkan compute, SPIR-V generated from
`zxva_vk.comp` by `gen_zxva_vk_spv.sh`; a stub that reports "unavailable"
when built without Vulkan) run the same integer source
(`zxva_stft_math.h` + `zxva_stft_kern.h`). They are bit-exact by design, and
the analysis falls back to the CPU when no device is present. What has been
run so far:

| backend | status |
|---|---|
| CPU reference | all tests |
| Vulkan | lavapipe (llvmpipe, a CPU Vulkan device): 200 random plans (5,958,423 output words) identical to the CPU, whole analyses byte-identical; corpus bench 9 / 9 files byte-identical. **Not yet run on a hardware GPU.** |
| CUDA | builds without the toolkit; kernel compiles to PTX for sm_75 / sm_86 (RTX A6000) / sm_90 (clang); a CPU emulation of the CUDA kernel (block = host threads with barriers) is identical on 24 / 24 plans and catches removed barriers. **Not yet run on an NVIDIA GPU**: `test_zxva_cuda` says SKIP without a driver. |
| Metal | not written |

On an NVIDIA machine, `ZXVA_GPU_REQUIRE=all sh tools/zxva/test_zxva_gpu.sh`
turns every SKIP into a FAIL. It runs the CUDA test on the device and the
Vulkan test on the first discrete GPU (`ZXVA_VK_DEVICE=n` picks one).

**Benchmark** (`tools/zxva/zxva_bench.c`, build line in its header):

```
sh kernel/src/vaudio/gen_zxva_vk_spv.sh /tmp/zxva_vk_spv.h /tmp/zxva_vk.spv
cc -std=c11 -O2 -DTEST_HOST -Ikernel/include -Ikernel/src/modbind -Ikernel/src/vaudio \
   -Ikernel/src/media -Ikernel/src/vshape -DZXVA_VK_SPV_H='"/tmp/zxva_vk_spv.h"' \
   tools/zxva/zxva_bench.c kernel/src/vaudio/zxva_analyse.c kernel/src/vaudio/zxva.c \
   kernel/src/vaudio/zxva_cuda.c kernel/src/vaudio/zxva_vk.c kernel/src/media/zui_audio.c \
   kernel/src/vshape/zxvs.c kernel/src/robin_debanks/sha256.c -ldl -o zxva_bench
./zxva_bench --backends cpu,cuda,vulkan --jsonl bench.jsonl --summary summary.json --out out/ CORPUS...
```

For each audio file and backend it writes one JSON line: file, CID, backend,
device, ms, kernel ms, ZXVA bytes, SNR (cdB), band error (cdB), residual
exactness, and byte identity with the CPU result. Every file also gets a ZXVS
line (`kernel/src/vshape/ZXVS.md`). One run on synthetic 2 s clips (44.1 /
48 kHz, residual on; CPU and lavapipe on the same machine, so the times say
nothing about GPU speed):

| file | source | ZXVA | SNR | band err | exact | lavapipe = CPU |
|---|---|---|---|---|---|---|
| piano 16-bit WAV / FLAC | 176,444 / 35,887 B | 43,266 B | 40.64 dB | 1.29 dB | yes | yes |
| piano 24-bit WAV | 264,644 B | 128,456 B | 40.63 dB | 1.28 dB | yes | yes |
| vowel WAV / AIFF | 192,044 / 192,054 B | 138,500 B | 8.35 dB | 4.01 dB | yes | yes |
| stereo chord + noise WAV / FLAC | 352,844 / 171,082 B | 177,242 B | 26.60 dB | 2.39 dB | yes | yes |
| noise bursts | 176,444 B | 117,436 B | -2.74 dB | 4.85 dB | yes | yes |
| clicks | 176,444 B | 55,812 B | -0.08 dB | 5.16 dB | yes | yes |
| piano MP3 | 33,062 B | refused (MP3 not decoded) | | | | |

Sizes include the lossless residual; the parametric part alone is 5.8 to
20 KB per clip. The residual coder is the plain Rice coder of section 3, so
for noise-like material ZXVA with residual is larger than FLAC.
