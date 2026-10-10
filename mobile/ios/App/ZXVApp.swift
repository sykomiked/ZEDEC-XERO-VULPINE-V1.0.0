// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
// Entry point for an iOS app target that depends on the ZXVKit package.
// Info.plist needs NSLocalNetworkUsageDescription, and broadcast pairing needs
// the com.apple.developer.networking.multicast entitlement.
import SwiftUI
import ZXVKit

@main
struct ZXVApp: App {
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup {
            RootView(model: model).onAppear { model.start() }
        }
    }
}
