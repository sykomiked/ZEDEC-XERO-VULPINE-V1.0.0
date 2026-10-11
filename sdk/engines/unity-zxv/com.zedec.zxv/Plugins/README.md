<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Native libraries go here

This package ships C# only. Put the two native libraries for each target
platform in this folder (or in your project's `Assets/Plugins`), built from
this repository:

| Platform | Files | Build |
|---|---|---|
| Linux x86_64 / arm64 | `libzxv.so`, `libzxv_emu.so` | `make -C sdk/libzxv && make -C sdk/engines/emu-state` |
| macOS (Editor and player) | `libzxv.dylib`, `libzxv_emu.dylib` | the same, on a Mac |
| Windows x86_64 | `zxv.dll`, `zxv_emu.dll` | not built or tested by this repository yet |
| iOS | `libzxv.a`, `libzxv_emu.a`, linked statically (`__Internal`) | not tested |
| Android | `libzxv.so`, `libzxv_emu.so` per ABI | not tested |

In each library's Inspector, tick only the platforms and CPU it was built
for. See `sdk/engines/unity-zxv/README.md`.
