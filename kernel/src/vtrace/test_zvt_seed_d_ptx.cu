/* test_zvt_seed_d_ptx.cu — the D-dimensional scaffold kernel to PTX, no
 * CUDA toolkit: the stand-in built-ins before the generated source, clang's
 * NVPTX target. The emulator has proven the kernel computes the CPU's bits
 * from 2D to 144D; this proves the GPU would run the same source. */
#include "test_zvt_cuda_ptx.h"
#include "zvt_seed_dgen.cu"