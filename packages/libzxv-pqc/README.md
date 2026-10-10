# libzxv-pqc

Post-quantum primitives from the ZXV kernel as one static library and one
header, `zxv_pqc.h`.

| Standard | Algorithm | Functions |
|---|---|---|
| FIPS 203 | ML-KEM-768 | `mlkem768_keygen`, `mlkem768_encaps`, `mlkem768_decaps` |
| FIPS 203 | ML-KEM-1024 | `pqm_mlkem1024_keygen`, `_encaps`, `_decaps`, `_check_ek`, `_check_dk` |
| FIPS 204 | ML-DSA-65 | `pq_mldsa65_keygen`, `_sign`, `_verify` |
| FIPS 204 | ML-DSA-87 | `pqm_mldsa87_keygen`, `_sign`, `_verify` |
| FIPS 205 | SLH-DSA-SHAKE-128s | `pq_slh128s_keygen`, `_sign`, `_verify`, `_sign_ctx`, `_verify_ctx` |
| FIPS 205 | SLH-DSA-SHAKE-256s | `pqm_slh256s_keygen`, `_sign`, `_verify` |
| FIPS 202 | SHA3-256/512, SHAKE128/256 | `sha3_256`, `sha3_512`, `shake128`, `shake256`, `shake128_*` |

## Build

From the repository root:

```sh
make -C packages pqc          # build/libzxv-pqc/libzxv-pqc.a + build/libzxv-pqc/include/
make -C packages check-pqc    # symbol check + smoke program
cc -Ipackages/build/libzxv-pqc/include app.c packages/build/libzxv-pqc/libzxv-pqc.a
```

The library is compiled from the kernel sources in place: `kernel/src/mlkem`
(ML-KEM-768 and Keccak), `kernel/src/pqsec/pq_mldsa65.c`, `pq_slhdsa.c`,
`pq_mlkem1024.c`, `pq_mldsa87.c`, `pq_slh256s.c`, and the vendored reference
code under `kernel/src/pqsec/mldsa`, `slhdsa` and `mlkem1024` (see their
`LICENSE` and `README.zxv` files). Nothing is copied into this directory.
HQC-5 and the hybrid `pq_matrix` layer are not included.

## Properties

- No allocation. Every buffer is the caller's; the largest are the
  signatures (29,792 bytes for SLH-DSA-256s), so keep them off a small stack.
- No randomness source. Every function that needs randomness takes it as an
  argument; pass fresh bytes from your platform's CSPRNG.
- Undefined symbols: only `memcpy`, `memset` and `memcmp` (`make check`
  fails on anything else). Compiled with `-ffreestanding`.
- Not thread-safe: ML-DSA key generation passes its seed to the reference
  code through a static buffer. Serialise calls.

## Status: tested, not verified, not certified

What is tested, in the ZXV tree's `make -C kernel verify-all`:
`test_mlkem_kat` (ML-KEM-768 against NIST ACVP vectors), `test_pq_kat`
(ML-DSA-65 and SLH-DSA-128s against ACVP vectors), `test_pq_matrix`
(ML-KEM-1024, ML-DSA-87, SLH-DSA-256s against ACVP vectors), and this
package's smoke program (SHA3-256 and ML-KEM-768 keygen known answers,
round trips and tamper checks).

What it is not: this code is not formally verified, has not been through
FIPS 140-3 / CMVP validation or any other certification, and has had no
independent side-channel or constant-time review beyond what the vendored
reference implementations provide. Do not describe a product built on it
as FIPS-validated.
