# Image input on the integer engine: SmolVLM-256M (2026-10-12)

The integer engine now answers questions about images. A photo is scaled,
encoded by SmolVLM's SigLIP image encoder and Idefics3 projector into 64
vectors, and those vectors go into the language model in place of token
embeddings. Everything is integer arithmetic and gives the same bits on every
kernel set, thread count and architecture.

## Models

Weights are never in git. `bench/tensor/fetch_pinned.py smolvlm` downloads them
into `~/.cache/zxv/models` and deletes any file whose SHA-256 differs.

| File | Bytes | SHA-256 | Licence |
|---|---|---|---|
| ggml-org/SmolVLM-256M-Instruct-GGUF `SmolVLM-256M-Instruct-Q8_0.gguf` | 175054528 | `2a31195d3769c0b0fd0a4906201666108834848db768af11de1d2cef7cd35e65` | Apache-2.0 |
| ggml-org/SmolVLM-256M-Instruct-GGUF `mmproj-SmolVLM-256M-Instruct-Q8_0.gguf` | 103769856 | `7e943f7c53f0382a6fc41b6ee0c2def63ba4fded9ab8ed039cc9e2ab905e0edd` | Apache-2.0 |
| ggml-org/SmolVLM-256M-Instruct-GGUF `mmproj-SmolVLM-256M-Instruct-f16.gguf` (set `smolvlm-f16`) | 190031616 | `0802360aca1748f112ea510b8ff277c65b1361c8ef30ed89b83c9c7a60d08e96` | Apache-2.0 |

The upstream model, HuggingFaceTB/SmolVLM-256M-Instruct, is Apache-2.0, so
commercial use is allowed.

## What was built

- `kernel/src/tensor/zt_nn.{h,c}` (T23): blocks shared by the image, speech-in
  and speech-out encoders. It has matrices of any GGUF type and row length,
  LayerNorm, GroupNorm, GELU/SiLU/sigmoid from Q30 tables
  (`gen_nn_tables.py`), multi-head attention (causal or not, self or cross),
  1-D convolution and depthwise convolution. Float matrices (F32/F16/BF16) are
  read from their own bits and kept as two 8-bit blocks, a main block plus a
  residual block, so the weights are accurate to about 2^-15 of each block's
  largest value. A caller can turn the residual block off for speed.
- `kernel/src/tensor/zt_vision.{h,c}` (T24): the SigLIP encoder, pixel
  shuffle, Idefics3 projector, and an integer area-average resize.
- `zt_model_eval_embd` / `zt_model_embed`: the language model takes rows of
  embeddings mixed with tokens. The logits are bit-equal to the token path
  when the rows are the token embeddings (test_zt_model).
- `zt_tok`: GPT-2 and SmolLM pre-tokenizer splits. It also handles
  vocabularies that lack some byte tokens, as SmolLM's does.
- `bench/tensor/zt_vlm.c` (`make -C bench zt_vlm`) is the driver.
  `bench/tensor/compare/ref_vision.py` is the float64 reference.
- verify-all now runs test_zt_vision on the C reference, every x86 SIMD set
  with and without threads, ASan/UBSan and arm64 NEON under qemu. It also
  runs a freestanding build of zt_nn and zt_vision for aarch64
  (`-mgeneral-regs-only`) and i386, where only `zt_` symbols may be external.

## Results

Image: llama.cpp's `tools/mtmd/test-1.jpeg` (640 x 488, converted to PPM; not
in the repository). Prompt: zt_vlm's default, "Describe this image in one sentence." The model was the Q8_0 language model with the Q8_0 projector.

| | Answer |
|---|---|
| llama.cpp `llama-mtmd-cli` | In this image we can see a newspaper. |
| ZXV integer engine | In this image we can see a newspaper. |

Bits. Every run below printed embd_fnv `59cb58559458d6e7` and logits_fnv
`c4dc9c3e16c25aab`:

- x86-64, C reference
- x86-64, AVX-512 VNNI, 4 threads
- arm64, NEON, 4 threads (qemu-aarch64)

Accuracy of the 64 x 576 image vectors against a float64 forward pass on the
same dequantised weights and the same resized pixels:

| Projector file | max abs error | RMS error / RMS | min cosine |
|---|---|---|---|
| Q8_0 | 0.024 | 0.032% | 0.9999996 |
| f16 | 0.027 | 0.031% | 0.9999996 |

The output RMS is 6.7. Before the float residual blocks were added, the Q8_0
projector run measured 0.27% RMS error and 0.23 max abs error (its patch
matrix is F32). That is about 9 times worse, with the same answer.

Speed on this cloud container (x86-64 AVX-512 VNNI, 4 threads):

| Step | Time |
|---|---|
| Image encode, ZXV | 2.3 s |
| Image encode, llama.cpp | about 2.1 s per chunk |
| Prompt of 82 positions | 0.36 s |
| Generation | 90 tokens/s |

The encoder is dominated by attention over 1024 patches.

llama.cpp feeds SmolVLM one tile plus a global view, while ZXV feeds the
global view only (64 image tokens). Both give the same answer here. Tiling is
a possible follow-up for large documents.

## Reproduce

```
python3 bench/tensor/fetch_pinned.py smolvlm
make -C bench zt_vlm
M=~/.cache/zxv/models/ggml-org_SmolVLM-256M-Instruct-GGUF
/tmp/zxv_bench/zt_vlm $M/SmolVLM-256M-Instruct-Q8_0.gguf $M/mmproj-SmolVLM-256M-Instruct-Q8_0.gguf \
    image.ppm --threads 4 --dump-rgb /tmp/rgb.bin --dump-embd /tmp/embd.bin
python3 bench/tensor/compare/ref_vision.py $M/mmproj-SmolVLM-256M-Instruct-Q8_0.gguf /tmp/rgb.bin /tmp/embd.bin
```
