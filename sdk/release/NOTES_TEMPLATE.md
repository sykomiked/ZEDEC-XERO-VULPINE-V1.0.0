<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# ZXV SDKs {{VERSION}}

**Status: draft {{CHANNEL}}, built {{DATE}} from commit `{{COMMIT}}` (tag `{{TAG}}`).**
This draft was produced by the `sdk-release` workflow. A maintainer reviews
it before anything is published, and nothing here has been pushed to a
package registry (PyPI, Maven Central, NuGet, npm, crates.io, RubyGems or
pub.dev). Registry publishing is a separate step that only the owner
approves; see `sdk/RELEASING.md`.

Every SDK wraps the same C library, libzxv (ABI {{CORE_VERSION}}), and is
tested against the same answer key, `sdk/conformance/vectors.json`.

## What is attached

| Ecosystem | Files | Notes |
|---|---|---|
| C ABI (libzxv) | `libzxv-{{VERSION}}-<platform>.tar.gz` | Header, shared and static library for linux-x86_64, linux-arm64, macos-arm64 and macos-x86_64. |
| C ABI source | `libzxv-{{VERSION}}-src.tar.gz` | libzxv, the kernel sources it compiles and the conformance vectors; `make -C sdk/libzxv`. |
| Python | `zxv_sdk-{{VERSION}}.tar.gz`, `zxv_sdk-*.whl` | Wheels bundle the matching libzxv; the sdist does not. Linux wheels carry plain `linux_*` tags, not `manylinux`. |
| Java / Kotlin / Scala (FFM) | `zxv-{{VERSION}}.jar` (+ `-sources`, `-javadoc`) | The jar carries libzxv for the four platforms above under `/native/<os>-<arch>/`. |
| Kotlin (JVM, JNI) | `zxv-kotlin-{{VERSION}}.jar` (+ `-sources`) | The JNI shim is not inside; build it from `sdk/mobile/kotlin/jni`. |
| Android | `zxv-android-{{VERSION}}.aar` | arm64-v8a and x86_64. |
| .NET | `Zxv.Native.*.nupkg`, `Zxv.Sdk.*.nupkg`, `Zxv.AspNetCore.*.nupkg` | `Zxv.Native` carries libzxv under `runtimes/{linux-x64,linux-arm64,osx-arm64,osx-x64}/native/`. |
| Spring Boot 3 | `zxv-spring-boot-starter-*.jar` | Depends on `org.zedec:zxv:{{VERSION}}`. |
| TypeScript / JavaScript | `zedec-zxv-{{VERSION}}.tgz`, `zedec-zxv-nestjs-*.tgz` | The WebAssembly backend is inside; the Node-API addon is compiled at install time and falls back to WebAssembly if that fails. |
| Rust | `zxv-sys-{{VERSION}}.crate`, `zxv-{{VERSION}}.crate` | Link against libzxv from the tarballs above. |
| Ruby | `zxv-{{VERSION}}.gem` | Pure Ruby over ffi; libzxv is not bundled. |
| PHP | `zxv-php-{{VERSION}}.zip` | Source for Composer (path or VCS repository); needs `ext-ffi`. |
| Dart / Flutter | `zxv-dart-{{VERSION}}.tar.gz`, `dart-pub-publish-dry-run.txt` | Source package plus the `dart pub publish --dry-run` report. |
| Swift | `zxv-swift-{{VERSION}}.tar.gz` | Source. `Package.swift` lives in `sdk/swift`, so SwiftPM cannot consume the repository URL directly. |
| Go | none | Use the module tag `{{GO_TAG}}` (created by the maintainer, see below). |
| Django | `django_zxv-*.tar.gz`, `django_zxv-*.whl` | Pure Python. |
| C++, Solidity, kits, pillars, engines, embedded | `*-src-{{VERSION}}.tar.gz` | Source tarballs that keep the repository layout, so they unpack next to the libzxv source tarball. |
| Integrity | `SHA256SUMS`, `zxv-sdk-{{VERSION}}.spdx.json` | SHA-256 of every file; an SPDX 2.3 SBOM of the source tree. |

{{ASSETS}}

Check the downloads with:

```sh
sha256sum -c SHA256SUMS          # macOS: shasum -a 256 -c SHA256SUMS
```

## Honest limits

- **No certification.** Nothing in this release is certified, validated or
  audited by any payment scheme, regulator, standards body or laboratory.
  The post-quantum and Ed25519 code is the vendored reference
  implementation; it is not FIPS 140-3 validated and has not had an
  independent audit.
- **Advisory only for safety-critical uses.** Features that touch clinical
  rules, power grids, vehicles, actuators or other life-safety systems
  produce advice for a human to approve. They never act on their own. The
  embedded and RTOS kits make no safety-standard or real-time claims.
- **Capabilities are least privilege, not a sandbox.** They are checked on
  every call, but any code in the same process can create its own root
  capability or read memory. Real enforcement needs a process boundary.
  Tri-Isolate is a process boundary, not a VM; z-ffi is not a sandbox.
- **Payment networks.** The message formats are designed to support
  integration with networks such as PAPSS and SWIFT. ZXV is not connected
  to them, has no partnership with them, and makes no claim of compliance
  with or endorsement by them.
- **Some calls return `ZXV_E_NOTIMPL`** in this build (for example LoRA,
  netting cycles and card issuing). Ask the library with `zxv_features`.

## Platforms not tested by this release

- **Windows.** libzxv does not build on Windows yet (it uses POSIX threads
  and the Makefile is Unix-only), so there are no Windows binaries.
- **macOS x86_64** binaries are cross-compiled on an Apple-silicon runner;
  their tests run only if Rosetta 2 is present there (see `BUILDINFO.txt`
  in the tarball), and the macOS x86_64 Python wheel is not smoke-tested.
- **Linux** binaries are built on Ubuntu 24.04 (glibc 2.39). Older
  distributions are not tested.
- **Android:** the AAR is built and linted, not run on a device or emulator.
- **iOS and Flutter apps:** no xcframework is attached and no app was run.
- **Unity:** the runtime is tested on .NET, not inside the Unity editor.
- **Godot:** tested headless on Linux only.
- **Embedded:** QEMU, the FreeRTOS POSIX simulator and Zephyr native_sim /
  qemu_cortex_m3 only. Nothing has run on real hardware.

## Go module tag

Go modules in a subdirectory are versioned by a prefixed tag. After this
release is published, the maintainer pushes:

```sh
git tag {{GO_TAG}} {{COMMIT}} && git push origin {{GO_TAG}}
```

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.
