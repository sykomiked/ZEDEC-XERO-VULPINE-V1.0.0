// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import SwiftUI
import ZXVRPC

enum Screen: String, CaseIterable, Identifiable, Hashable {
    case studio, ledger, mesh, storefront, settings
    var id: String { rawValue }

    var title: String {
        switch self {
        case .studio: return "Local AI Studio"
        case .ledger: return "Sicily Ledger"
        case .mesh: return "P2P Mesh"
        case .storefront: return "Storefront & Escrow"
        case .settings: return "Settings"
        }
    }

    var icon: String {
        switch self {
        case .studio: return "bubble.left"
        case .ledger: return "building.columns"
        case .mesh: return "point.3.connected.trianglepath.dotted"
        case .storefront: return "storefront"
        case .settings: return "slider.horizontal.3"
        }
    }
}

@MainActor
struct RootView: View {
    @EnvironmentObject var store: AppStore
    @State private var screen: Screen? = .studio

    var body: some View {
        NavigationSplitView {
            VStack(alignment: .leading, spacing: 0) {
                VStack(alignment: .leading, spacing: 4) {
                    Text("ZXV SWARM").font(.system(size: 16, weight: .bold)).kerning(0.6)
                    Text("\(store.version?.app ?? "zxvd") on this Mac")
                        .font(.caption).foregroundStyle(Theme.muted)
                }
                .padding(.horizontal, 18)
                .padding(.vertical, 16)
                List(Screen.allCases, selection: $screen) { s in
                    Label(s.title, systemImage: s.icon).tag(s)
                }
                .scrollContentBackground(.hidden)
                DaemonBadge().padding(14)
            }
            .background(Theme.sidebar)
            .navigationSplitViewColumnWidth(min: 210, ideal: 230, max: 280)
        } detail: {
            Group {
                switch store.link {
                case .connected:
                    ScrollView {
                        content
                            .padding(.horizontal, 32)
                            .padding(.vertical, 28)
                            .frame(maxWidth: 1400, alignment: .leading)
                    }
                case .connecting:
                    ProgressView("Connecting to zxvd")
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                case .down(let why):
                    NotConnected(reason: why)
                }
            }
            .background(Theme.ground)
            .foregroundStyle(Theme.text)
        }
    }

    @ViewBuilder private var content: some View {
        switch screen ?? .studio {
        case .studio: StudioView()
        case .ledger: LedgerView()
        case .mesh: MeshView()
        case .storefront: StorefrontView()
        case .settings: SettingsView()
        }
    }
}

@MainActor
struct DaemonBadge: View {
    @EnvironmentObject var store: AppStore

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                Circle().fill(store.link == .connected ? Theme.good : Theme.warn).frame(width: 8, height: 8)
                Text(store.link == .connected ? "zxvd connected" : "zxvd not connected")
                    .font(.callout)
            }
            if let v = store.version {
                Text("\(v.protocol) · libzxv \(v.libzxv)")
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(Theme.muted)
            }
            if let ok = store.booksOK {
                Text(ok ? "Books balanced" : "Books check failed")
                    .font(.caption)
                    .foregroundStyle(ok ? Theme.muted : Theme.warn)
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Theme.border))
    }
}

@MainActor
struct NotConnected: View {
    @EnvironmentObject var store: AppStore
    let reason: String

    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: "bolt.horizontal.circle").font(.system(size: 40)).foregroundStyle(Theme.muted)
            Text("zxvd is not running").font(.title2.weight(.semibold))
            Text(reason).foregroundStyle(Theme.muted).multilineTextAlignment(.center)
            Text("ZXV Studio talks to zxvd at\n\(store.socketPath)")
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(Theme.muted)
                .multilineTextAlignment(.center)
                .textSelection(.enabled)
            Button(store.stoppedByOwner ? "Start zxvd" : "Try again") {
                Task { await store.connect() }
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
        }
        .padding(40)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
