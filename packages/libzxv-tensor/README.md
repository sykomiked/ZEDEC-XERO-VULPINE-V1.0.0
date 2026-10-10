# libzxv-tensor

The ZXV integer tensor engine as one static library and one header,
`zxv_tensor.h`.

| Header | What it is |
|---|---|
| `zt.h` | Q16 fixed point, 8-bit blocks of 32 (`zt_q8_t`), dot product, matvec |
| `zt_gguf.h` | GGUF reader: metadata, tensors, F32/F16/BF16/Q8_0/Q4_0/Q5_0/Q5_1/Q4_K/Q6_K to Q16 |
| `zt_tok.h` | the GGUF tokenizer (BPE, from the file's vocabulary) |
| `zt_rope.h` | rotary position embedding tables |
| `zt_lattice.h` | E8 and Leech lattice quantisers |
| `zt_model.h` | the qwen2 / qwen3 / llama forward pass over a mapped GGUF file |

## Build

```sh
make -C packages tensor          # build/libzxv-tensor/libzxv-tensor.a + include/
make -C packages check-tensor    # symbol check + smoke program
cc -Ipackages/build/libzxv-tensor/include app.c packages/build/libzxv-tensor/libzxv-tensor.a
```

Built in place from `kernel/src/tensor/zt*.c` (not the tests); the E8 tables
come from `kernel/src/e8/e8_lattice.h` at compile time only.

## Properties

- Integer arithmetic only: no floating point, no 64-bit division helper
  (`zt_udiv64` instead), no `__int128`.
- No allocation: the model and its state live in arenas the caller sizes
  with `zt_model_arena_bytes` / `zt_model_state_bytes`.
- The archive has no undefined symbols at all (`make check` verifies it),
  so it links into a freestanding target as is. The kernel tree also
  compiles these sources for aarch64-none-elf and i386-none-elf.
- One thread, scalar C.

## Status: tested, not verified, not certified

Tested in `make -C kernel verify-all` by `test_zt`, `test_zt_gguf` (against
gguf-py's writer and dequantiser), `test_zt_q5` (Q5_0/Q5_1 bit for bit
against golden values from ggml's reference decoder), `test_zt_tok`,
`test_zt_rope`, `test_zt_lattice`, `test_zt_model` (against a numpy
reference on small random models) and `test_zt_audit` (sanitizer build),
plus this package's smoke program.

Not done: no real model has been run end to end, so real-model accuracy and
speed are unmeasured (zt_model.h gives the measured error on the test models
and an estimate of speed). Results are not llama.cpp bit for bit. The code is
not formally verified and not certified for any use.
