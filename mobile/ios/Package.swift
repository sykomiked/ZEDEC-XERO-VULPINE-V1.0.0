// swift-tools-version:5.9
// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZXVKit: SwiftUI screens + a Swift wrapper over the C core (mobile/core/zxv_mobile.h).
// UNBUILT: written without an Apple SDK. To build on a Mac:
//   1. mobile/core/build_mobile_core.sh ios      (makes mobile/core/build/ZXVCore.xcframework)
//   2. open this package in Xcode, or add it to an iOS app target and use App/ZXVApp.swift
//      as the app's entry point.
import PackageDescription

let package = Package(
    name: "ZXVKit",
    platforms: [.iOS(.v17)],
    products: [.library(name: "ZXVKit", targets: ["ZXVKit"])],
    targets: [
        .binaryTarget(name: "ZXVCore", path: "../core/build/ZXVCore.xcframework"),
        .target(name: "ZXVKit", dependencies: ["ZXVCore"], path: "Sources/ZXVKit"),
    ]
)
