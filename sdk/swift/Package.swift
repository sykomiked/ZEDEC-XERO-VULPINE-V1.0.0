// swift-tools-version: 6.0
// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The ZXV Swift SDK: a system library target (CZxv) over zxv_api.h and the
// Swift API (Zxv). libzxv is linked, not bundled; see README.md for the
// macOS .app (libzxv.dylib) and iOS (xcframework) layouts.
//
// Where libzxv is found at build time:
//   1. ZXV_LIBRARY_PATH (a directory holding libzxv.dylib / libzxv.so)
//   2. the in-repo build, ../libzxv/build
//   3. the system linker path
import Foundation
import PackageDescription

let packageDir = URL(fileURLWithPath: #filePath).deletingLastPathComponent().path
let env = ProcessInfo.processInfo.environment
let libDir = env["ZXV_LIBRARY_PATH"] ?? "\(packageDir)/../libzxv/build"
let hasLocalLib = FileManager.default.fileExists(atPath: libDir)

// Local builds pass -L and an rpath so `swift test` finds the library with
// no installation step. These are unsafe flags, which SwiftPM allows for a
// root or local-path package; a release build consumed from a registry uses
// the xcframework binary target described in the README instead.
let linkFlags: [LinkerSetting] = hasLocalLib
    ? [.unsafeFlags(["-L\(libDir)", "-Xlinker", "-rpath", "-Xlinker", libDir])]
    : []

let package = Package(
    name: "Zxv",
    platforms: [.macOS(.v13), .iOS(.v16)],
    products: [
        .library(name: "Zxv", targets: ["Zxv"]),
        .executable(name: "zxv-quickstart", targets: ["ZxvQuickstart"]),
    ],
    targets: [
        .systemLibrary(name: "CZxv", path: "Sources/CZxv"),
        .target(name: "Zxv", dependencies: ["CZxv"], linkerSettings: linkFlags),
        .executableTarget(name: "ZxvQuickstart", dependencies: ["Zxv"], linkerSettings: linkFlags),
        .testTarget(name: "ZxvTests", dependencies: ["Zxv", "CZxv"], linkerSettings: linkFlags),
    ],
    swiftLanguageModes: [.v6]
)
