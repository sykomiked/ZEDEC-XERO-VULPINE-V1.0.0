<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZEDEC Legacy Bridge — Language Bindings

One stable C ABI (`zx_legacy_api.h`) wraps the freestanding bridge modules in
`kernel/src/legacy/`. Every language here links the same `libzxlegacy` and calls
the same symbols. The ABI is append-only; `zx_legacy_abi_version()` returns the
contract version (currently 1).

## Building

```
./build_lib.sh            # builds libzxlegacy.{a,so}, runs the C self-test,
                          # then builds+runs each language example present
```

`build_lib.sh` compiles the bridge modules and the ABI wrapper, then builds and
runs whichever of the compiled examples have a compiler installed. It skips the
rest with a printed note.

## What was built and run in this container

| Language | File | Status |
|----------|------|--------|
| C (self-test) | `test_abi.c` | built + run, 14 checks pass |
| GnuCOBOL | `cobol/payvalidate.cob` + `cobol/txnrec.cpy` | built with `cobc` 3.1.2 + run; COMP-3 round-trips through the C library |
| Fortran 2003 | `fortran/zx_legacy_bind.f90` + `fortran/hfp_demo.f90` | built with `gfortran` 13.3 + run; HFP↔IEEE round-trips |
| Free Pascal | `pascal/zxdemo.pas` | built with `fpc` 3.2.2 + run |
| Ada 2012 | `ada/zx_legacy.ads` + `ada/zx_demo.adb` | built with `gnatmake` 13.3 + run |

## Document-only sketches (no compiler in this container)

These show the correct ABI declarations for environments we could not build
here. They are illustrative, not compiled or tested.

| Language | File | Target environment |
|----------|------|--------------------|
| PL/I | `examples/payvalidate.pli` | Enterprise PL/I on z/OS |
| ILE RPG | `examples/PAYVAL.rpgle` | IBM i |
| BASIC | `examples/hfpdemo.bas` | FreeBASIC / any BASIC with C FFI |

## Calling-convention notes

- Pass buffers by reference (their address) and scalars by value. In COBOL use
  `BY REFERENCE` for `PIC X`/packed fields and `BY VALUE` for `COMP-5` counts.
- GnuCOBOL resolves `CALL`ed names at run time. Because the program references
  the bridge symbols only through dynamic `CALL`, no `DT_NEEDED` is recorded, so
  the library must be made visible at run time — `build_lib.sh` does this with
  `LD_PRELOAD=$OUT/libzxlegacy.so`. On z/OS, bind the DLL instead.
- The ABI opens no sockets and authorizes no payment. A confirmation digit is a
  UI event; the platform's configured strong authentication still governs money
  movement. See `../docs/LEGACY_BRIDGE.md`.
