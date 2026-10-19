/* test_zvt_seed_ptx.cu — the seed-unfold kernel to PTX, no CUDA toolkit.
 * The PTX proof: concatenate the stand-in built-ins (test_zvt_cuda_ptx.h)
 * before the generated kernel source and compile with clang's NVPTX target,
 * exactly as test_vtrace_gpu.sh does for the vectoriser's kernels. The GPU
 * (an RTX A6000, sm_86) then has the same source NVRTC would compile, and
 * the emulator has already proven it computes the CPU's bits. */
#include "test_zvt_cuda_ptx.h"
#include "zvt_seed_gen.cu"