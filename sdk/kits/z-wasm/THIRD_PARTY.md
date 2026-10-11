<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Third-party code in Z-Wasm

Z-Wasm vendors exactly one third-party component. Everything else in
`sdk/kits/z-wasm` is Apache-2.0 and carries an SPDX header.

## wasm3 (the WebAssembly interpreter)

| | |
|---|---|
| Project | wasm3, <https://github.com/wasm3/wasm3> |
| Version | tag `v0.9.0`, commit `0cd38327f0c721e75172f4f1eeb55854dc0517af` (2026-08-24) |
| Licence | MIT ([`third_party/wasm3/LICENSE`](third_party/wasm3/LICENSE), copied unchanged) |
| Copyright | 2019 Steven Massey, Volodymyr Shymanskyy |
| Compatible with Apache-2.0 | yes: MIT is permissive; keep its copyright notice and licence text with any redistribution, source or binary |
| Where | `third_party/wasm3/source/` |
| Modifications | none: the files are byte-identical to the tag (checked by `make verify-third-party`) |

### Why wasm3

- A small (~0.5 MB of C), embeddable interpreter with no JIT, so it runs
  where executable memory is not allowed, and its whole attack surface is
  C code we build with our own flags and sanitizers.
- It is fuzzed upstream in OSS-Fuzz, has a load-time type validator
  (`d_m3EnableValidation`), traps on out-of-bounds memory, and bounds its
  own native stack use (`d_m3MaxNativeStack`).
- Its float support can be compiled out (`d_m3HasFloat=0`), which matches
  the ZXV no-float rule; Z-Wasm builds it that way.
- MIT, so it can ship inside an Apache-2.0 product.

WAMR (Apache-2.0) was the other candidate; it is larger and more complex to
embed for an interpreter-only build. The host API (`include/zwasm.h`) does
not expose wasm3 types, so the runtime can be replaced later without
changing the kit API.

**Maintenance status, honestly.** The wasm3 README says the project is in a
"minimal maintenance phase": its author reviews and merges pull requests
but is not developing new features. v0.9.0 is a recent tagged release.
Track upstream security fixes; if wasm3 stops receiving them, move the host
to WAMR behind the same `zwasm.h`.

### What is vendored

Only the interpreter core: the WASI, libc, tracer and uvwasi API files,
the extensions, the test-suite and the platform ports are left out.
Build flags (see the `Makefile`): `-std=gnu99 -O2 -w -Dd_m3HasFloat=0
-Dd_m3LogOutput=0`, plus the sanitizers under `SANITIZE=1`.

### Checksums (SHA-256 of `third_party/wasm3/source/*` and `LICENSE`)

```sha256
00435c456c9a0bb956b4068036ec02b3bcbbf5061a412e518216725e28becc58  source/m3_bind.c
01ae23645d7e045f1f7e0d8d6e08eef0d3d6e572225e80b69cd426ce491fd604  source/m3_code.c
2370482bf254cd276547c478528050a3bcee6fb0f9524623f5c171aae4d5e8f0  source/m3_code.h
a607c82507edf738e2621970a077896291247afabbe6e5e82a96693b9cb9056a  source/m3_compile.c
e2371686bf3f377c2d8016786b368954bdf5bd51c3b37b6267fd2f2fee0de774  source/m3_compile.h
b84841ce6182c70332f53e9a096b4ca7f731a3b2d4daaba0fd523865d54054e1  source/m3_config.h
105cafc36aae0a545844c1db9affa359d414abc7bcf61c8e9b190747c30d3118  source/m3_config_platforms.h
980f94170fb8a748fb298509fbef4388640c9a66f54b54d3beaa22fb3daba996  source/m3_core.c
3e7818242316214bef86c06a28215bc3f2c6edadfad6340ad4b66594c6ed3f05  source/m3_core.h
694f32446738d87a3d9b45dba30fdf0e6ce45485f0ec266c81887156592daafa  source/m3_env.c
93ffae0fa497360485c1f3581c30fa99dcde5ef169fae84227b19d7998ea58e5  source/m3_env.h
e6beeab90bed0f30d1d14e8a31434b9878d82356579ddda747b05fb2d6d6fda4  source/m3_exception.h
9e5d6adb9c757c0e8fa06c8c18c335b1f8ccc16922b65693071f1878124f09ec  source/m3_exec.c
8d427cb0fdd128be763245691fd8c86846d6906448643568c8849895b23a67ca  source/m3_exec.h
d1af4d9bcee94f9416e250a262a4e892b094d0cb02833b846ec7f70e58efa66b  source/m3_exec_defs.h
35290133a7fdf94175c8694b40de843be66daa8b831a40dbaab0c98c69c388c0  source/m3_function.c
6acdb0d65f67e3cfff5c8968f19555d92f9a05347701f72c8a751d5ef330b21f  source/m3_function.h
4d4cb31d68b7df8584ac25fdace33cb3cda5dff6fc91a0ca6ad6faf907d848c5  source/m3_info.c
ebb3d06fc4b2213aa0b5d8fb70d133f17ab6a01b2ae3fa6a78e0b8ba0f96d485  source/m3_info.h
8a123a5c79bccb3e96d46df451c0afdccfb67eb44c887efbfb38bf2f54e8d83a  source/m3_math_utils.h
a23907d3d34d02d3b91204957df619abfe353686656c609eef21f10b777d36c1  source/m3_module.c
337c7d0cbba341a545caf5c803066eac1d014914cdf2dd5359f0b75935648729  source/m3_parse.c
3f0049f1e97a793298733d0eb1c93f3e27bf62cc00c50936e9045ae7ce0c3e03  source/m3_validate.c
bc5259773e07bdeb3654116b2587f5fbe4d806218858acf9d96a126603a27e97  source/m3_validate.h
6452dfb74b54eb77a5483597a7d6abe0a7779344facfb581f89ab6813e8767e9  source/wasm3.h
98f33c6bf16ab94a75082fe13f12f66abce3d3f4e7b8384e5d23f905ed44f12f  source/wasm3_defs.h
c67aa5c0d77ea831c9a9a025fcbdb69d8241da5d7c673243ae34b1a660afcda2  LICENSE
```

### Updating wasm3

1. Check out the new tag in a scratch directory and read its changelog
   and security notes.
2. Copy the same file list into `third_party/wasm3/source/`, and the
   `LICENSE`.
3. Regenerate the checksum block above (`sha256sum source/* LICENSE` in
   `third_party/wasm3`), and update the version and commit in the table.
4. `make clean test`, `make SANITIZE=1 OUT=build/san test`, `make fuzz`,
   `make test-node`.
