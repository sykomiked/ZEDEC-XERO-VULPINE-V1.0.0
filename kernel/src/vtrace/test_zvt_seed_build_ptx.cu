/* test_zvt_seed_build_ptx.cu — the build-scaffolding kernel to PTX, no CUDA
 * toolkit: concatenate the stand-in built-ins before the generated source,
 * compile with clang's NVPTX target (the A6000 is sm_86). The emulator has
 * proven the kernel computes the CPU's bits; this proves the GPU would run
 * the same source. */
#include "test_zvt_cuda_ptx.h"
#include "zvt_seed_build_gen.cu"