// swift-tools-version: 5.9
// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZXV Studio: the native SwiftUI app for macOS. It drives zxvd, the ZXV
// daemon, over its JSON-RPC control socket (docs/ZXVD.md,
// api/zxvd.openrpc.json) and never links the kernel itself.
//
//   ZXVRPC     Foundation only: the socket client, typed results, exact
//              minor-unit amounts, net commands, starting zxvd. Builds and
//              is tested on Linux too, against the real daemon.
//   ZXVStudio  The SwiftUI app (macOS 13 and later). Mac only.
import PackageDescription

var targets: [Target] = [
    .target(name: "ZXVRPC"),
    .testTarget(name: "ZXVRPCTests", dependencies: ["ZXVRPC"]),
]
var products: [Product] = [.library(name: "ZXVRPC", targets: ["ZXVRPC"])]

#if os(macOS)
targets.append(.executableTarget(name: "ZXVStudio", dependencies: ["ZXVRPC"]))
products.append(.executable(name: "ZXVStudio", targets: ["ZXVStudio"]))
#endif

let package = Package(
    name: "ZXVStudio",
    platforms: [.macOS(.v13)],
    products: products,
    targets: targets
)
