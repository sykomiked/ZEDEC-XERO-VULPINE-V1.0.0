# Task: vectoriser GPU proofs and corpus benchmarks on the A6000 server

From the "Native app toolkit build" thread. Nothing here pushes to GitHub,
installs system packages or starts CI.

## What it is

ZXV keeps every file in a vector form next to the original:

- **Pictures and films:** pictures become ZXVG, films become ZXVM.
  Sources: `kernel/src/vtrace`, `tools/vtrace/vtrace.c`.
- **Audio:** ZXVA. Sources: `kernel/src/vaudio`, `tools/zxva/zxva_bench.c`.
- **Any file:** ZXVS, a data shape descriptor. Source: `kernel/src/vshape`.

The per-pixel and per-sample stages have CUDA and Vulkan backends. These are
designed to give the same bits as the integer CPU reference.

So far they have only been checked without a real GPU:

- Vulkan on lavapipe.
- CUDA compiled to PTX and run in a CPU emulator.

This run is the first on real NVIDIA hardware. It also measures fidelity on
a corpus. The original is always kept by CID, and the vector form "scales
cleanly". It never adds detail the source did not have.

## Steps

1. Get the branch:

       git clone https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0 ~/zxv-vector-src
       cd ~/zxv-vector-src && git checkout claude/project-thread-sfklol-vector

2. Check the tools. No nvcc is needed.

   - **CUDA:** `nvidia-smi` and `ldconfig -p | grep -E 'libcuda|libnvrtc'`.
     If NVRTC is missing, install the user-level wheel
     `pip install --user nvidia-cuda-nvrtc-cu12`. Then point `ZT_NVRTC_LIB`
     (and `ZVT_NVRTC_LIB` / `ZXVA_NVRTC_LIB` if set) at the
     `libnvrtc.so.12` it installed.
   - **Vulkan:** `vulkaninfo --summary` should list the A6000. You also need
     `glslangValidator` and the Vulkan headers (apt `vulkan-tools
     glslang-tools libvulkan-dev`). Ask the user before installing anything
     system-wide.
   - **Python:** `python3` with numpy, which the visual corpus generator
     uses. `flac` is optional and adds FLAC copies of the audio corpus.

3. Run it. It takes roughly 10 to 30 minutes.

       bash tools/vector/run_vector_bench.sh [EXTRA_CORPUS_DIR]

   It writes only under `~/zxv-vector` (set `WORK=` to move it). The steps:

   - **Corpus:**
     - Generates pictures and films (PNG, JPEG, GIF, BMP, Y4M, AVI, MP4).
     - Generates audio (WAV at 44.1, 48 and 96 kHz, 16 and 24-bit, plus FLAC
       when `flac` is installed).
     - Copies a few repo files as the data corpus, including the 5.4 MB
       `cards.tsv`.
   - **Tests:** the toolkit unit suites, then both GPU proof scripts with
     every backend required. A missing CUDA or Vulkan backend counts as a
     failure.
   - **Benchmarks:** every corpus through every backend and preset, with JSON
     per file and a summary per corpus.
   - **Extra corpus:** an optional `EXTRA_CORPUS_DIR` adds your own pictures,
     films and audio.

4. Copy `~/zxv-vector/results/` back and tell the "Native app toolkit build"
   thread. If the server cannot reach `/mnt/project-files`, paste
   `run.json`, `steps.jsonl`, the three `*_summary.json` and any failing
   `*.log`. The results folder holds:

   - `host.txt`, `steps.jsonl` and `run.json`;
   - `visual.jsonl` with `visual_summary.json`;
   - `audio.jsonl` with `audio_summary.json`;
   - `data.jsonl`, `data_1pct.jsonl` and `data_summary.json`;
   - a `<step>.log` for every step.

## What to look for

- **GPU against CPU:** every `bit_exact_vs_cpu` should be true. Any false is
  a bug to report, with the file name.
- **Backend availability:** `gpu_vtrace` and `gpu_zxva` should PASS with
  CUDA and Vulkan both available. The reasons are in their logs.
- **Speed:** the summaries carry GPU speed-ups. These are the first real
  numbers; earlier runs were CPU against CPU and meant nothing.
- **Fidelity:** PSNR, SSIM and SNR come from the integer metrics. An EXACT
  flag is only ever set on a pixel-identical or sample-identical result.
