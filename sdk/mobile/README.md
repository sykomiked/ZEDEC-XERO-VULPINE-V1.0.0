<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV mobile SDKs

Two SDKs for phones, over the same [libzxv](../libzxv/) C ABI and the same
[design rules](../docs/SDK_DESIGN.md) as every other ZXV SDK, tested against
the same [`vectors.json`](../conformance/vectors.json), and two web kits on
the [TypeScript SDK](../typescript/).

| SDK | Folder | Binds through | Ships to | Tests |
|---|---|---|---|---|
| Kotlin (Android + JVM) | [`kotlin`](kotlin/) | a small JNI shim (Android has no Java FFM) | AAR with `arm64-v8a` and `x86_64` | 34, host JVM |
| Dart (Flutter + Dart) | [`dart`](dart/) | `dart:ffi`, hand-written and drift-checked | your app's `jniLibs` / iOS xcframework | 33, Dart VM |
| Z-Web (`@zedec/zxv-web`) | [`z-web`](z-web/) | the wasm build of `@zedec/zxv` in a Web Worker; exact `"num/den"` message protocol | browsers (ES module + worker) | 32, Node `worker_threads` + real wasm |
| React-ZXV (`@zedec/zxv-react`) | [`react-zxv`](react-zxv/) | an injected backend: Z-Web, the `@zedec/zxv` addon, or zxvd JSON-RPC | React DOM, React Native / Expo | 41, jsdom + real addon + real wasm |

For iOS outside Flutter, use the [Swift SDK](../swift/).

## Shipping libzxv per ABI

| Target | Build | Package |
|---|---|---|
| Android `arm64-v8a`, `x86_64` | [`kotlin/scripts/build-libzxv-android.sh`](kotlin/scripts/build-libzxv-android.sh) (NDK r26+, API 24) | Kotlin: inside the AAR. Flutter: `android/app/src/main/jniLibs/<abi>/libzxv.so` |
| iOS device + simulator | [`dart/scripts/build-libzxv-xcframework.sh`](dart/scripts/build-libzxv-xcframework.sh) (macOS + Xcode) | `Zxv.xcframework`, static, linked into the app |
| Desktop JVM / Dart VM | `make -C sdk/libzxv` | `ZXV_LIBRARY_PATH` or next to the program |

Both scripts drive `sdk/libzxv`'s own Makefile, so there is one list of
sources. The Android script adds two workarounds for the NDK (no
`getrandom` below API 28, no `libpthread`); see the Kotlin README.

## What was tested

- Kotlin + JNI shim: 34 tests on a desktop JVM (JDK 21 and 25), against the
  real libzxv. The Android AAR builds (AGP 8.13, NDK r27) and Android lint
  finds no API-level errors; nothing ran on a device or emulator.
- Dart: 33 tests with `dart test` on the Dart VM (Dart 3.13), and
  `dart analyze --fatal-infos`. No Flutter app was built; nothing ran on a
  device.
- Z-Web: 32 tests in Node 22 (`node:test`, a `worker_threads` worker
  running the real `zxv.wasm`), and the example page once in headless
  Chromium. No other browser, no mobile browser.
- React-ZXV: 41 tests in Node 22 (jsdom, `@testing-library/react`) on a
  fake backend, the real Node-API addon and the real wasm worker; the zxvd
  adapter against an in-memory imitation of the daemon. No React Native
  device, simulator or Expo build.

CI: [`.github/workflows/sdk-mobile.yml`](../../.github/workflows/sdk-mobile.yml)
(Linux and macOS host tests, the AAR build, the iOS xcframework build, and
one `web-kits` job for Z-Web and React-ZXV).
