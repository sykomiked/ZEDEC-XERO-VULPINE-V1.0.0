// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
// Entry point for an iOS app target that depends on the ZXVKit package.
// Info.plist needs NSLocalNetworkUsageDescription, and broadcast pairing needs
// the com.apple.developer.networking.multicast entitlement. Add
// assets/fonts/ascw-golden to the app target as a folder reference named
// "Fonts" (Copy Bundle Resources): ZXVKit registers ASCW Golden from it, and
// OFL.txt in it is the fonts' licence, which must ship with them.
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
